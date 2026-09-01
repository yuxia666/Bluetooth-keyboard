/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * 灯带启停事件：控制 WS2812 灯带开/关
 */

#ifndef _LED_STRIP_EN_EVENT_H_
#define _LED_STRIP_EN_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct led_strip_en_event {
	struct app_event_header header;
	bool enabled;                 /* true=开, false=关 */
};

APP_EVENT_TYPE_DECLARE(led_strip_en_event);

static inline void led_strip_en_event_submit(bool enabled)
{
	struct led_strip_en_event *event = new_led_strip_en_event();

	event->enabled = enabled;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _LED_STRIP_EN_EVENT_H_ */
