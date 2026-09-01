/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * 主题色更新事件：上位机/设置页修改主题后，LED 模块更新颜色
 */

#ifndef _THEME_RGB_UPDATE_EVENT_H_
#define _THEME_RGB_UPDATE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <zephyr/drivers/led_strip.h>

#ifdef __cplusplus
extern "C" {
#endif

struct theme_rgb_update_event {
	struct app_event_header header;
	struct led_rgb theme_color;   /* r/g/b */
};

APP_EVENT_TYPE_DECLARE(theme_rgb_update_event);

static inline void theme_rgb_update_event_submit(uint8_t r, uint8_t g,
						 uint8_t b)
{
	struct theme_rgb_update_event *event = new_theme_rgb_update_event();

	event->theme_color.r = r;
	event->theme_color.g = g;
	event->theme_color.b = b;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _THEME_RGB_UPDATE_EVENT_H_ */
