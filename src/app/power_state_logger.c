/*
 * 蓝牙小键盘 - 电源状态日志模块
 *
 * 监听：
 *   power_down_event -> 进入低功耗，打印 "enter low power"
 *   wake_up_event    -> 退出低功耗，打印 "exit low power"
 *
 * 同时维护当前低功耗状态，供 button_wake 模块判断按键时该唤醒还是 keep alive。
 */

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <app_event_manager.h>
#include <caf/events/power_event.h>

#include "power_state_logger.h"

static bool power_suspended;

bool power_state_is_suspended(void)
{
	return power_suspended;
}

static bool handle_power_down_event(const struct power_down_event *evt)
{
	ARG_UNUSED(evt);

	if (!power_suspended) {
		power_suspended = true;
		printk("enter low power\n");
	}

	return false;
}

static bool handle_wake_up_event(const struct wake_up_event *evt)
{
	ARG_UNUSED(evt);

	if (power_suspended) {
		power_suspended = false;
		printk("exit low power\n");
	}

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_power_down_event(aeh)) {
		return handle_power_down_event(cast_power_down_event(aeh));
	}

	if (is_wake_up_event(aeh)) {
		return handle_wake_up_event(cast_wake_up_event(aeh));
	}

	return false;
}

APP_EVENT_LISTENER(power_state_logger, app_event_handler);
APP_EVENT_SUBSCRIBE(power_state_logger, power_down_event);
APP_EVENT_SUBSCRIBE(power_state_logger, wake_up_event);
