/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _SET_PROTOCOL_EVENT_H_
#define _SET_PROTOCOL_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_PROTOCOL_BOOT    0
#define HID_PROTOCOL_REPORT  1

struct set_protocol_event {
	struct app_event_header header;
	uint8_t protocol;
};

APP_EVENT_TYPE_DECLARE(set_protocol_event);

static inline void set_protocol_event_submit(uint8_t protocol)
{
	struct set_protocol_event *evt = new_set_protocol_event();

	evt->protocol = protocol;
	APP_EVENT_SUBMIT(evt);
}

#ifdef __cplusplus
}
#endif

#endif /* _SET_PROTOCOL_EVENT_H_ */
