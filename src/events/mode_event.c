/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/mode_event.h"

static void log_mode_event(const struct app_event_header *aeh)
{
	const struct mode_event *event = cast_mode_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "mode=%d", event->mode);
}

static void profile_mode_event(struct log_event_buf *buf,
			       const struct app_event_header *aeh)
{
	const struct mode_event *event = cast_mode_event(aeh);

	nrf_profiler_log_encode_uint8(buf, (uint8_t)event->mode);
}

APP_EVENT_INFO_DEFINE(mode_event,
		  ENCODE(NRF_PROFILER_ARG_U8),
		  ENCODE("mode"),
		  profile_mode_event);

APP_EVENT_TYPE_DEFINE(mode_event,
		  log_mode_event,
		  &mode_event_info,
		  APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
