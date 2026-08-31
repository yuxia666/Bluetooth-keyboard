/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Power manager module — battery voltage measurement via voltage-divider
 * sensor driver, charging state via IP5306, events via CAF.
 *
 * 对齐参考 keyboard 工程 src/power_mgr.c：
 *  - voltage-divider 传感器驱动（vbatt 节点自动管理 BAT_ADC_EN）
 *  - 滑动窗口滤波
 *  - LUT 计算 SOC（3200~4100mV，100mV 步进）
 *  - battery_event{level, state}，仅在明显变化时发布
 *  - 监听 power_down/wake_up 挂起/恢复采样
 *  - 随低功耗调用 ip5306_suspend/resume 停止/恢复保活
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>

#include "events/battery_event.h"
#include <drivers/ip5306.h>

#define MODULE power_mgr
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_LOG_DEFAULT_LEVEL);

/* ================================================================= *
 *  Configuration macros (all tunables here, no Kconfig/def files)
 * ================================================================= */

/* Sampling */
#define SAMPLE_INTERVAL_MS 1000
#define FILTER_SAMPLES 4

/* Battery voltage → SoC LUT (3.2 V … 4.1 V, step 100 mV) */
#define BATTERY_MIN_MV 3200
#define BATTERY_MAX_MV 4100
#define BATTERY_LUT_STEP_MV 100

/* Publish threshold */
#define LEVEL_CHANGE_THRESHOLD 5

/* Device tree nodes */
#define VBATT_DT_NODE DT_NODELABEL(vbatt)
#define IP5306_DT_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(injoinic_ip5306)

/* ================================================================= *
 *  Voltage → SoC lookup table
 * ================================================================= */

static const uint8_t battery_voltage_to_soc[] = {
	/* 3200 */ 0,
	/* 3300 */ 2,
	/* 3400 */ 4,
	/* 3500 */ 8,
	/* 3600 */ 15,
	/* 3700 */ 30,
	/* 3800 */ 50,
	/* 3900 */ 70,
	/* 4000 */ 90,
	/* 4100 */ 100,
};

static uint8_t voltage_to_percent(uint32_t mv)
{
	if (mv >= BATTERY_MAX_MV) {
		return 100;
	}
	if (mv <= BATTERY_MIN_MV) {
		return 0;
	}

	size_t idx = (mv - BATTERY_MIN_MV) / BATTERY_LUT_STEP_MV;
	return battery_voltage_to_soc[idx];
}

/* ================================================================= *
 *  State
 * ================================================================= */

static const struct device *vbatt_dev;
static const struct device *ip5306_dev;

static struct k_work_delayable sample_work;
static bool suspended;
static bool first_sample;
static uint8_t last_level = 255;
static uint8_t last_state = 0xFF;

/* ================================================================= *
 *  Sliding-window averaging filter
 * ================================================================= */

static uint32_t filter_buf[FILTER_SAMPLES];
static uint8_t filter_idx;
static uint8_t filter_count;

static int read_battery_mv_avg(uint32_t *mv)
{
	struct sensor_value val;
	uint32_t sample;
	uint64_t sum;
	int err;

	/* Read one sample from voltage-divider sensor */
	err = sensor_sample_fetch(vbatt_dev);
	if (err) {
		return err;
	}
	err = sensor_channel_get(vbatt_dev, SENSOR_CHAN_VOLTAGE, &val);
	if (err) {
		return err;
	}

	sample = (uint32_t)(val.val1 * 1000 + val.val2 / 1000);

	/* Push into sliding window, evict oldest */
	filter_buf[filter_idx] = sample;
	filter_idx = (filter_idx + 1) % FILTER_SAMPLES;
	if (filter_count < FILTER_SAMPLES) {
		filter_count++;
	}

	/* Average over current window */
	sum = 0;
	for (uint8_t i = 0; i < filter_count; i++) {
		sum += filter_buf[i];
	}
	*mv = (uint32_t)(sum / filter_count);
	return 0;
}

/* ================================================================= *
 *  Periodic sampling
 * ================================================================= */

static void sample_fn(struct k_work *work)
{
	uint32_t mv;
	uint8_t level = 0;
	uint8_t state = BATTERY_STATE_IDLE;
	int err;

	/* 1. Read voltage (FILTER_SAMPLES averaged) */
	err = read_battery_mv_avg(&mv);
	if (err) {
		LOG_ERR("Battery read failed: %d", err);
		state = BATTERY_STATE_ERROR;
		goto publish;
	}

	/* 2. Voltage → percentage */
	level = voltage_to_percent(mv);

	/* 周期性打印电池电压与 SOC（RTT 可观察） */
	LOG_INF("Battery: %u mV, SOC %u%%", mv, level);

	/* 3. Read charging state via IP5306 (if available) */
	if (ip5306_dev) {
		enum ip5306_charge_state cs;

		if (ip5306_charge_status(ip5306_dev, &cs) == 0) {
			switch (cs) {
			case IP5306_CHARGE_ACTIVE:
				state = BATTERY_STATE_CHARGING;
				break;
			case IP5306_CHARGE_FULL:
				state = BATTERY_STATE_FULL;
				break;
			default:
				state = BATTERY_STATE_IDLE;
				break;
			}
		}
	}

publish:
	/* 4. Publish on first sample / significant change */
	if (first_sample ||
		(level > last_level ? level - last_level : last_level - level) >= LEVEL_CHANGE_THRESHOLD ||
		state != last_state) {

		battery_event_submit(level, state);
		last_level = level;
		last_state = state;
		first_sample = false;
	}

	/* 5. Reschedule */
	if (!suspended) {
		k_work_reschedule(&sample_work, K_MSEC(SAMPLE_INTERVAL_MS));
	}
}

/* ================================================================= *
 *  Init
 * ================================================================= */

static int init(void)
{
	/* Voltage-divider sensor */
	vbatt_dev = DEVICE_DT_GET(VBATT_DT_NODE);
	if (!device_is_ready(vbatt_dev)) {
		LOG_ERR("Voltage divider device not ready");
		return -ENODEV;
	}

	/* IP5306 (optional — without it, charging status is unavailable) */
	ip5306_dev = DEVICE_DT_GET(IP5306_DT_NODE);
	if (!device_is_ready(ip5306_dev)) {
		LOG_WRN("IP5306 not found — charging status disabled");
		ip5306_dev = NULL;
	}

	k_work_init_delayable(&sample_work, sample_fn);
	first_sample = true;
	k_work_reschedule(&sample_work, K_MSEC(2000));

	module_set_state(MODULE_STATE_READY);
	LOG_INF("Power manager initialised");

	return 0;
}

/* ================================================================= *
 *  CAF event handler
 * ================================================================= */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event =
			cast_module_state_event(aeh);

		if (event->state == MODULE_STATE_READY &&
			event->module_id == MODULE_ID(main)) {
			init();
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (suspended) { return false; }
		suspended = true;
		k_work_cancel_delayable(&sample_work);

		/* 随低功耗停止 IP5306 保活 */
		if (ip5306_dev) {
			ip5306_suspend(ip5306_dev);
		}

		module_set_state(MODULE_STATE_OFF);
		return false;
	}

	if (is_wake_up_event(aeh)) {
		suspended = false;
		first_sample = true;

		/* 恢复 IP5306 保活 */
		if (ip5306_dev) {
			ip5306_resume(ip5306_dev);
		}

		k_work_reschedule(&sample_work, K_MSEC(100));
		module_set_state(MODULE_STATE_READY);
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
