/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * 三模切换模块 — 通过 ADC AIN5 采样拨档电压，判定 USB / 2.4G / BLE。
 *
 * 硬件（原理图 + 网表 + E73 手册交叉验证）：
 *   - AIN5 = P0.29（E73 模块引脚 8）
 *   - SK-13D07-4 拨档三档分压：USB=0V / 2.4G=1.65V / BLE=3.3V
 *   - 阈值：USB < 825mV，2.4G 825~2475mV，BLE >= 2475mV
 *
 * 行为：
 *   - 首次采样即发布（上电按拨档自动定初始模式）
 *   - 连续 3 次采样一致才切换（去抖，防止拨档抖动反复上报）
 *   - 落在阈值间隙时保持当前模式
 *   - 低功耗挂起/恢复采样
 *   - RTT 打印 Mode: USB/2.4G/BLE
 *
 * 屏幕接口（预留）：display 模块订阅 mode_event 即可显示当前模式，
 * 本模块不做任何屏幕操作。
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>

#include "events/mode_event.h"

#define MODULE mode
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_LOG_DEFAULT_LEVEL);

/* 采样周期与去抖 */
#define SAMPLE_INTERVAL_MS  100
#define DEBOUNCE_SAMPLES    3

/* 电压阈值 (mV)：
 * USB < 825；2.4G 825~2475；BLE >= 2475
 * （825mV = 3.3V * 1/4，2475mV = 3.3V * 3/4，与档位电压 0/1.65/3.3V 对应）
 */
#define VOL_USB_MAX         825
#define VOL_24G_MIN         825
#define VOL_24G_MAX         2475
#define VOL_BLE_MIN         2475

static const struct adc_dt_spec adc_ch = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static enum keyboard_mode current_mode;
static uint8_t debounce_cnt;
static bool suspended;
static bool first_sample;

static enum keyboard_mode voltage_to_mode(int32_t mv)
{
	if (mv < VOL_USB_MAX) {
		return KEYBOARD_MODE_USB;
	}
	if ((mv >= VOL_24G_MIN) && (mv <= VOL_24G_MAX)) {
		return KEYBOARD_MODE_2_4G;
	}
	if (mv >= VOL_BLE_MIN) {
		return KEYBOARD_MODE_BLE;
	}

	/* 落在阈值间隙中，保持当前模式 */
	return current_mode;
}

static const char *mode_str(enum keyboard_mode mode)
{
	switch (mode) {
	case KEYBOARD_MODE_USB:
		return "USB";
	case KEYBOARD_MODE_2_4G:
		return "2.4G";
	case KEYBOARD_MODE_BLE:
		return "BLE";
	default:
		return "UNKNOWN";
	}
}

static void publish_mode(enum keyboard_mode mode)
{
	struct mode_event *event = new_mode_event();

	event->mode = mode;
	APP_EVENT_SUBMIT(event);

	LOG_INF("Mode: %s", mode_str(mode));
}

static void sample_fn(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(sample_work, sample_fn);

static void sample_fn(struct k_work *work)
{
	int32_t val_mv;
	uint16_t buf;
	struct adc_sequence seq = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	enum keyboard_mode new_mode;
	int err;

	if (suspended) {
		return;
	}

	adc_sequence_init_dt(&adc_ch, &seq);
	err = adc_read_dt(&adc_ch, &seq);
	if (err) {
		LOG_ERR("ADC read failed: %d", err);
		goto reschedule;
	}

	val_mv = (int32_t)buf;
	err = adc_raw_to_millivolts_dt(&adc_ch, &val_mv);
	if (err) {
		LOG_ERR("Raw to mV failed: %d", err);
		goto reschedule;
	}

	new_mode = voltage_to_mode(val_mv);

	if (first_sample) {
		first_sample = false;
		current_mode = new_mode;
		publish_mode(current_mode);
		goto reschedule;
	}

	if (new_mode == current_mode) {
		debounce_cnt = 0;
		goto reschedule;
	}

	debounce_cnt++;
	if (debounce_cnt >= DEBOUNCE_SAMPLES) {
		debounce_cnt = 0;
		current_mode = new_mode;
		publish_mode(current_mode);
	}

reschedule:
	k_work_schedule(&sample_work, K_MSEC(SAMPLE_INTERVAL_MS));
}

static int init(void)
{
	if (!adc_is_ready_dt(&adc_ch)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	int err = adc_channel_setup_dt(&adc_ch);
	if (err) {
		LOG_ERR("ADC channel setup failed: %d", err);
		return err;
	}

	current_mode = KEYBOARD_MODE_USB;
	debounce_cnt = 0;
	suspended = false;
	first_sample = true;

	k_work_schedule(&sample_work, K_MSEC(SAMPLE_INTERVAL_MS));
	module_set_state(MODULE_STATE_READY);

	return 0;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event = cast_module_state_event(aeh);

		if (event->state == MODULE_STATE_READY &&
		    event->module_id == MODULE_ID(main)) {
			init();
		}

		return false;
	}

	if (is_power_down_event(aeh)) {
		if (suspended) { return false; }
		k_work_cancel_delayable(&sample_work);
		suspended = true;
		module_set_state(MODULE_STATE_OFF);
		return false;
	}

	if (is_wake_up_event(aeh)) {
		suspended = false;
		first_sample = true;
		k_work_schedule(&sample_work, K_MSEC(SAMPLE_INTERVAL_MS));
		module_set_state(MODULE_STATE_READY);
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
