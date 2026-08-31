/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_CONSUMER_EVENT_H_
#define _HID_CONSUMER_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hid_consumer_event {
	struct app_event_header header;
	uint16_t usage;           /* Consumer usage, 0x0000 = release */
};

APP_EVENT_TYPE_DECLARE(hid_consumer_event);

/** @brief Submit a consumer control event */
static inline void hid_consumer_event_submit(uint16_t usage)
{
	struct hid_consumer_event *event = new_hid_consumer_event();

	event->usage = usage;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_CONSUMER_EVENT_H_ */
