/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BATTERY_EVENT_H_
#define _BATTERY_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Battery states */
#define BATTERY_STATE_IDLE      0
#define BATTERY_STATE_CHARGING  1
#define BATTERY_STATE_FULL      2
#define BATTERY_STATE_ERROR     3

/* Read macros */
#define BATTERY_LEVEL(evt)  ((evt)->level)
#define BATTERY_STATE(evt)  ((evt)->state)

struct battery_event {
	struct app_event_header header;
	uint8_t level;
	uint8_t state;
};

APP_EVENT_TYPE_DECLARE(battery_event);

/** @brief Submit a battery event (static inline helper). */
static inline void battery_event_submit(uint8_t level, uint8_t state)
{
	struct battery_event *event = new_battery_event();

	event->level = level;
	event->state = state;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _BATTERY_EVENT_H_ */
