/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/set_protocol_event.h"

static void log_set_protocol_event(const struct app_event_header *aeh)
{
	const struct set_protocol_event *evt = cast_set_protocol_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "protocol=%s",
			      evt->protocol == HID_PROTOCOL_BOOT ? "boot" : "report");
}

static void profile_set_protocol_event(struct log_event_buf *buf,
				       const struct app_event_header *aeh)
{
	const struct set_protocol_event *evt = cast_set_protocol_event(aeh);

	nrf_profiler_log_encode_uint8(buf, evt->protocol);
}

APP_EVENT_INFO_DEFINE(set_protocol_event,
		  ENCODE(NRF_PROFILER_ARG_U8),
		  ENCODE("protocol"),
		  profile_set_protocol_event);

APP_EVENT_TYPE_DEFINE(set_protocol_event,
		  log_set_protocol_event,
		  &set_protocol_event_info,
		  APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
