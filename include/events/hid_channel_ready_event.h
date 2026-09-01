/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * HID 通道 ready 事件：transport 层把通道就绪/失联状态通知给 hid_scheduler
 */

#ifndef _HID_CHANNEL_READY_EVENT_H_
#define _HID_CHANNEL_READY_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct hid_channel_ready_event {
	struct app_event_header header;
	enum hid_channel channel;   /* 哪个通道 */
	bool ready;                 /* true=就绪, false=失联 */
};

APP_EVENT_TYPE_DECLARE(hid_channel_ready_event);

static inline void hid_channel_ready_event_submit(enum hid_channel channel,
						  bool ready)
{
	struct hid_channel_ready_event *evt = new_hid_channel_ready_event();

	evt->channel = channel;
	evt->ready = ready;
	APP_EVENT_SUBMIT(evt);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_CHANNEL_READY_EVENT_H_ */
