/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_key_event.h"

static void log_hid_key_event(const struct app_event_header *aeh)
{
	const struct hid_key_event *event = cast_hid_key_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "proto=%u size=%u",
			      event->protocol, event->report_size);
}

static void profile_hid_key_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
	const struct hid_key_event *event = cast_hid_key_event(aeh);

	nrf_profiler_log_encode_uint8(buf, event->protocol);
	nrf_profiler_log_encode_uint8(buf, event->report_size);
}

APP_EVENT_INFO_DEFINE(hid_key_event,
		      ENCODE(NRF_PROFILER_ARG_U8, NRF_PROFILER_ARG_U8),
		      ENCODE("protocol", "report_size"),
		      profile_hid_key_event);

APP_EVENT_TYPE_DEFINE(hid_key_event,
		      log_hid_key_event,
		      &hid_key_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
