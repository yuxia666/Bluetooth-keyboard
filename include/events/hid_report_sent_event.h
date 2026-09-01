/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_REPORT_SENT_EVENT_H_
#define _HID_REPORT_SENT_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hid_report_sent_event {
	struct app_event_header header;
	enum hid_channel channel;   /* 完成通道 */
};

APP_EVENT_TYPE_DECLARE(hid_report_sent_event);

static inline void hid_report_sent_event_submit(enum hid_channel channel)
{
	struct hid_report_sent_event *evt = new_hid_report_sent_event();

	evt->channel = channel;
	APP_EVENT_SUBMIT(evt);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_REPORT_SENT_EVENT_H_ */
