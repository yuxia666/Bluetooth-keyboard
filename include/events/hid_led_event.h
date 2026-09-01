/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _HID_LED_EVENT_H_
#define _HID_LED_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hid_led_event {
	struct app_event_header header;
	uint8_t led_state;
};

APP_EVENT_TYPE_DECLARE(hid_led_event);

static inline void hid_led_event_submit(uint8_t led_state)
{
	struct hid_led_event *evt = new_hid_led_event();

	evt->led_state = led_state;
	APP_EVENT_SUBMIT(evt);
}

#ifdef __cplusplus
}
#endif

#endif /* _HID_LED_EVENT_H_ */
