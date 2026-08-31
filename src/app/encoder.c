/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * 编码器模块 — 基于 Zephyr QDEC sensor 驱动（硬件 QDEC，不占 GPIOTE）
 * 产生通用 encoder_event (方向 + 卡点步数)，由上层决定用途。
 *
 * 对齐参考 keyboard 工程 src/encoder.c：
 *  - nRF52840 QDEC 外设，SENSOR_CHAN_ROTATION
 *  - 微度累积，避免跨 report 丢步（2+2 边沿跨窗口假事件 bug）
 *  - encoder_event{steps}：正=顺时针，负=逆时针，绝对值=卡点数
 *  - 监听 power_down/wake_up 挂起/恢复
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>

#include "events/encoder_event.h"

#define MODULE encoder
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_LOG_DEFAULT_LEVEL);

/* ================================================================ *
 *  配置 — EC11: 20 卡点/圈, 每卡点 4 个 QDEC 边沿
 * ================================================================ */

#define DETENTS_PER_REV        20
#define QDEC_EDGES_PER_DETENT  4
#define QDEC_STEPS_PER_REV     (DETENTS_PER_REV * QDEC_EDGES_PER_DETENT)  /* 80 */
#define DEG_PER_DETENT         (360 / DETENTS_PER_REV)                    /* 18° */
#define UDEG_PER_DETENT        (DEG_PER_DETENT * 1000000)                 /* 18,000,000 µ° */

/* 设备树节点 */
#define QDEC_NODE DT_NODELABEL(qdec0)

/* ================================================================ *
 *  状态
 * ================================================================ */

static const struct device *qdec_dev;
static struct sensor_trigger trig;
static int32_t accum_udeg; /* 微度累积器, 保留跨 report 的亚卡点残余 */
static bool powered_down;

/* ================================================================ *
 *  Trigger handler — QDEC REPORTRDY 回调
 * ================================================================ */

static void qdec_trigger_handler(const struct device *dev,
				 const struct sensor_trigger *trig)
{
	struct sensor_value val;
	int8_t detent_steps;

	/* 读取并清零硬件累加器 */
	if (sensor_sample_fetch(dev) != 0) {
		return;
	}

	/*
	 * 获取增量角度 (自上次 sample_fetch 以来的旋转角度)
	 * val.val1 > 0 → CW, val.val1 < 0 → CCW
	 */
	if (sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &val) != 0) {
		return;
	}

	/*
	 * 用微度 (val1 * 10^6 + val2) 累积, 零精度损失
	 * 每卡点 = 18° = 18,000,000 微度
	 * 累积器保留跨 report 的亚卡点残余, 避免 2+2 边沿
	 * 跨窗口时产生两个虚假事件的 bug
	 *
	 * 注意：此处取反以修正旋钮旋转方向（硬件相位相反时）。
	 */
	accum_udeg -= (int32_t)val.val1 * 1000000 + val.val2;

	detent_steps = (int8_t)(accum_udeg / UDEG_PER_DETENT);
	if (detent_steps != 0) {
		/* 临时调试打印：方向 + 角度（度）+ 卡点数，方向以 detent_steps 为准 */
		int32_t deg = (int32_t)val.val1;

		if (deg < 0) {
			deg = -deg;
		}
		if (detent_steps > 0) {
			LOG_INF("旋钮: 顺时针 %d°, %d 卡点", deg, detent_steps);
		} else {
			LOG_INF("旋钮: 逆时针 %d°, %d 卡点", deg, -detent_steps);
		}
		encoder_event_submit(detent_steps);
		accum_udeg -= detent_steps * UDEG_PER_DETENT;
	}
}

/* ================================================================ *
 *  初始化
 * ================================================================ */

static int encoder_init(void)
{
	qdec_dev = DEVICE_DT_GET(QDEC_NODE);
	if (!device_is_ready(qdec_dev)) {
		LOG_ERR("QDEC device not ready");
		return -ENODEV;
	}

	trig.type = SENSOR_TRIG_DATA_READY;
	trig.chan = SENSOR_CHAN_ROTATION;

	if (sensor_trigger_set(qdec_dev, &trig, qdec_trigger_handler) != 0) {
		LOG_ERR("Failed to set QDEC trigger");
		return -EIO;
	}

	module_set_state(MODULE_STATE_READY);
	LOG_INF("Encoder initialised (EC11, %d detents/rev)", DETENTS_PER_REV);

	return 0;
}

/* ================================================================ *
 *  CAF 事件处理
 * ================================================================ */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *event =
			cast_module_state_event(aeh);

		if (event->state == MODULE_STATE_READY &&
		    event->module_id == MODULE_ID(main)) {
			encoder_init();
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (powered_down) { return false; }
		powered_down = true;
		module_set_state(MODULE_STATE_OFF);
		return false;
	}

	if (is_wake_up_event(aeh)) {
		powered_down = false;
		module_set_state(MODULE_STATE_READY);
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
