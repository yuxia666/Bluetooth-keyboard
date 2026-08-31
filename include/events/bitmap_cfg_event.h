/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BITMAP_CFG_EVENT_H_
#define _BITMAP_CFG_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FUNCKEY_BITMAP_SIZE 29

struct bitmap_cfg_event {
	struct app_event_header header;
	uint8_t usage_bitmap[FUNCKEY_BITMAP_SIZE];
};

APP_EVENT_TYPE_DECLARE(bitmap_cfg_event);

static inline void bitmap_cfg_event_submit(const uint8_t *bitmap)
{
	struct bitmap_cfg_event *event = new_bitmap_cfg_event();

	memcpy(event->usage_bitmap, bitmap, FUNCKEY_BITMAP_SIZE);
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _BITMAP_CFG_EVENT_H_ */
