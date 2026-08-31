/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _FUNCKEY_REPORT_EVENT_H_
#define _FUNCKEY_REPORT_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FUNCKEY_REPORT_SIZE 29

struct funckey_report_event {
	struct app_event_header header;
	uint8_t usage_bitmap[FUNCKEY_REPORT_SIZE];
};

APP_EVENT_TYPE_DECLARE(funckey_report_event);

static inline void funckey_report_event_submit(const uint8_t *bitmap)
{
	struct funckey_report_event *event = new_funckey_report_event();

	memcpy(event->usage_bitmap, bitmap, FUNCKEY_REPORT_SIZE);
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _FUNCKEY_REPORT_EVENT_H_ */
