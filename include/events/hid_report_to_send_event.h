/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_REPORT_TO_SEND_EVENT_H_
#define _HID_REPORT_TO_SEND_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <string.h>

#include "events/hid_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hid_report_to_send_event {
	struct app_event_header header;
	enum hid_channel channel;   /* 目标通道 */
	uint8_t report[29];
	uint8_t report_size;
};

APP_EVENT_TYPE_DECLARE(hid_report_to_send_event);

static inline void hid_report_to_send_event_submit(enum hid_channel channel,
						   const uint8_t *report,
						   uint8_t report_size)
{
	struct hid_report_to_send_event *evt =
		new_hid_report_to_send_event();

	evt->channel = channel;
	memcpy(evt->report, report, report_size);
	evt->report_size = report_size;
	APP_EVENT_SUBMIT(evt);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_REPORT_TO_SEND_EVENT_H_ */
