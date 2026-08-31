/*
 * 蓝牙小键盘 - 电池状态事件
 */

#ifndef _BATTERY_EVENT_H_
#define _BATTERY_EVENT_H_

#include <stdbool.h>
#include <stdint.h>

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 电池状态事件 */
struct battery_event {
	struct app_event_header header;

	/** 是否正在充电 */
	bool charging;

	/** 是否已充满 */
	bool full;

	/** 电池电压（mV） */
	uint16_t voltage_mv;

	/** SOC 百分比（0-100，按 10% 档位） */
	uint8_t soc;
};

APP_EVENT_TYPE_DECLARE(battery_event);

#ifdef __cplusplus
}
#endif

#endif /* _BATTERY_EVENT_H_ */
