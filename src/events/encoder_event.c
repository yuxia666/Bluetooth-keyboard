/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/encoder_event.h"

static void log_encoder_event(const struct app_event_header *aeh)
{
	const struct encoder_event *event = cast_encoder_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "steps=%d", event->steps);
}

static void profile_encoder_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
	const struct encoder_event *event = cast_encoder_event(aeh);

	nrf_profiler_log_encode_uint8(buf, (uint8_t)event->steps);
}

APP_EVENT_INFO_DEFINE(encoder_event,
		  ENCODE(NRF_PROFILER_ARG_U8),
		  ENCODE("steps"),
		  profile_encoder_event);

APP_EVENT_TYPE_DEFINE(encoder_event,
		  log_encoder_event,
		  &encoder_event_info,
		  APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
