/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/bitmap_cfg_event.h"

static void log_bitmap_cfg_event(const struct app_event_header *aeh)
{
	APP_EVENT_MANAGER_LOG(aeh, "bitmap updated");
}

APP_EVENT_TYPE_DEFINE(bitmap_cfg_event,
		      log_bitmap_cfg_event,
		      NULL,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
