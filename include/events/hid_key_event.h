/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_KEY_EVENT_H_
#define _HID_KEY_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <string.h>

#include "events/set_protocol_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_BOOT_REPORT_SIZE   8
#define HID_NKRO_REPORT_SIZE  29

struct hid_key_event {
	struct app_event_header header;
	uint8_t protocol;         /* HID_PROTOCOL_BOOT or HID_PROTOCOL_REPORT */
	uint8_t report[29];       /* Unified buffer: boot uses first 8 bytes */
	uint8_t report_size;      /* 8 for boot, 29 for NKRO */
};

APP_EVENT_TYPE_DECLARE(hid_key_event);

static inline void hid_key_event_submit(uint8_t protocol,
					const uint8_t *report,
					uint8_t report_size)
{
	struct hid_key_event *event = new_hid_key_event();

	event->protocol = protocol;
	memcpy(event->report, report, report_size);
	event->report_size = report_size;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_KEY_EVENT_H_ */
