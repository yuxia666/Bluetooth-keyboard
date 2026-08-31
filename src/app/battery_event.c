/*
 * 蓝牙小键盘 - 电池状态事件定义
 */

#include "battery_event.h"

APP_EVENT_TYPE_DEFINE(battery_event,
		      NULL,
		      NULL,
		      APP_EVENT_FLAGS_CREATE());
