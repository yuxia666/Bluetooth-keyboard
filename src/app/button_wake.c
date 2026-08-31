/*
 * 蓝牙小键盘 - 按键唤醒/保活模块
 *
 * 订阅 button_event：
 *   - 当前处于低功耗 suspended 状态 -> 发送 wake_up_event 唤醒系统
 *   - 当前未处于低功耗状态          -> 发送 keep_alive_event 重置空闲计时
 */

#include <stdbool.h>

#include <zephyr/kernel.h>

#include <app_event_manager.h>
#include <caf/events/button_event.h>
#include <caf/events/keep_alive_event.h>
#include <caf/events/power_event.h>

#include "power_state_logger.h"

static bool handle_button_event(const struct button_event *evt)
{
	/* 只在按下沿处理 */
	if (!evt->pressed) {
		return false;
	}

	if (power_state_is_suspended()) {
		APP_EVENT_SUBMIT(new_wake_up_event());
	} else {
		keep_alive();
	}

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_button_event(aeh)) {
		return handle_button_event(cast_button_event(aeh));
	}

	return false;
}

APP_EVENT_LISTENER(button_wake, app_event_handler);
APP_EVENT_SUBSCRIBE(button_wake, button_event);
