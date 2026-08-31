/*
 * 蓝牙小键盘 - 电池状态模块
 *
 * 功能：
 *   - 初始化 IP5306 PMIC 驱动
 *   - 周期性读取 IP5306 charging/full 状态
 *   - 读取电池电压（BAT_ADC，P0.31/AIN7）
 *   - 计算 SOC（3300mV -> 0%，4200mV -> 100%，按 10% 档位四舍五入）
 *   - 通过 battery_event 发布到 app_event_manager
 *
 * IP5306 keepalive 由驱动内部的 RTC2 + GPIOTE + PPI 硬件产生，
 * 本模块不参与 keepalive 脉冲定时。
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#include <app_event_manager.h>
#include <caf/events/power_event.h>
#include <drivers/ip5306.h>

#include "battery_event.h"

#define BATTERY_READ_INTERVAL_MS 8000
#define BATTERY_ADC_EN_SETTLE_US  100

#define VBATT_MIN_MV 3300
#define VBATT_MAX_MV 4200
#define VBATT_RANGE_MV (VBATT_MAX_MV - VBATT_MIN_MV)

/* 电池分压：R6(100k) + R8(100k) -> ADC 采中间点，倍率为 2 */
#define VBATT_DIVIDER 2

/* BAT_ADC_EN = P0.09（NF1 释放为 GPIO），高有效使能采样分压网络 */
static const struct gpio_dt_spec batt_adc_en = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
	.pin = 9,
	.dt_flags = GPIO_ACTIVE_HIGH,
};

static const struct adc_dt_spec adc_chan =
	ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(battery_volt), 0);

static int16_t adc_sample;
static struct k_work_delayable battery_work;
static bool battery_suspended;

static uint8_t voltage_to_soc(uint16_t voltage_mv)
{
	int32_t soc;

	if (voltage_mv <= VBATT_MIN_MV) {
		return 0;
	}
	if (voltage_mv >= VBATT_MAX_MV) {
		return 100;
	}

	soc = ((int32_t)(voltage_mv - VBATT_MIN_MV) * 100 + VBATT_RANGE_MV / 2) /
	      VBATT_RANGE_MV;

	/* 四舍五入到 10% 档位 */
	soc = ((soc + 5) / 10) * 10;

	if (soc > 100) {
		soc = 100;
	}

	return (uint8_t)soc;
}

static void battery_read(struct k_work *work)
{
	struct ip5306_status ip5306_status = {0};
	struct battery_event *event;
	int ret;
	uint16_t vbat_mv;
	uint8_t soc;

	ARG_UNUSED(work);

	if (battery_suspended) {
		return;
	}

	ret = ip5306_get_status(&ip5306_status);
	if (ret < 0) {
		printk("[battery] IP5306 状态读取失败: %d\n", ret);
	} else {
		printk("[battery] charging=%d full=%d\n",
		       ip5306_status.charging, ip5306_status.full);
	}

	/* 打开 BAT_ADC_EN，等待分压网络稳定后采样 */
	gpio_pin_set_dt(&batt_adc_en, 1);
	k_busy_wait(BATTERY_ADC_EN_SETTLE_US);

	struct adc_sequence sequence;

	adc_sequence_init_dt(&adc_chan, &sequence);
	sequence.buffer = &adc_sample;
	sequence.buffer_size = sizeof(adc_sample);

	ret = adc_read_dt(&adc_chan, &sequence);
	gpio_pin_set_dt(&batt_adc_en, 0);

	if (ret < 0) {
		printk("[battery] ADC 读取失败: %d\n", ret);
	} else {
		int32_t raw_mv = adc_sample;

		ret = adc_raw_to_millivolts_dt(&adc_chan, &raw_mv);
		if (ret < 0) {
			printk("[battery] ADC 换算失败: %d\n", ret);
		} else {
			vbat_mv = (uint16_t)(raw_mv * VBATT_DIVIDER);
			soc = voltage_to_soc(vbat_mv);

			printk("[battery] vbat=%u mV soc=%u%%\n", vbat_mv, soc);

			event = new_battery_event();
			event->charging = ip5306_status.charging;
			event->full = ip5306_status.full;
			event->voltage_mv = vbat_mv;
			event->soc = soc;
			APP_EVENT_SUBMIT(event);
		}
	}

	k_work_schedule(&battery_work, K_MSEC(BATTERY_READ_INTERVAL_MS));
}

static bool battery_module_handle_power_down(const struct power_down_event *evt)
{
	ARG_UNUSED(evt);

	battery_suspended = true;
	k_work_cancel_delayable(&battery_work);

	return false;
}

static bool battery_module_handle_wake_up(const struct wake_up_event *evt)
{
	ARG_UNUSED(evt);

	if (battery_suspended) {
		battery_suspended = false;
		k_work_schedule(&battery_work, K_MSEC(1000));
	}

	return false;
}

static bool battery_module_event_handler(const struct app_event_header *aeh)
{
	if (is_power_down_event(aeh)) {
		return battery_module_handle_power_down(cast_power_down_event(aeh));
	}

	if (is_wake_up_event(aeh)) {
		return battery_module_handle_wake_up(cast_wake_up_event(aeh));
	}

	return false;
}

APP_EVENT_LISTENER(battery_module, battery_module_event_handler);
APP_EVENT_SUBSCRIBE(battery_module, power_down_event);
APP_EVENT_SUBSCRIBE(battery_module, wake_up_event);

static int battery_module_init(void)
{
	int ret;

	ret = ip5306_init();
	if (ret < 0) {
		printk("[battery] IP5306 初始化失败: %d\n", ret);
	}

	if (!device_is_ready(adc_chan.dev)) {
		printk("[battery] ADC 设备未就绪\n");
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&adc_chan);
	if (ret < 0) {
		printk("[battery] ADC 通道配置失败: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&batt_adc_en, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("[battery] BAT_ADC_EN 配置失败: %d\n", ret);
		return ret;
	}

	k_work_init_delayable(&battery_work, battery_read);
	k_work_schedule(&battery_work, K_MSEC(1000));

	printk("[battery] 电池状态模块已启动\n");

	return 0;
}

SYS_INIT(battery_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
