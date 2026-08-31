/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_consumer_event.h"

static void log_hid_consumer_event(const struct app_event_header *aeh)
{
	const struct hid_consumer_event *event = cast_hid_consumer_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "usage=0x%04x", event->usage);
}

static void profile_hid_consumer_event(struct log_event_buf *buf,
				       const struct app_event_header *aeh)
{
	const struct hid_consumer_event *event = cast_hid_consumer_event(aeh);

	nrf_profiler_log_encode_uint16(buf, event->usage);
}

APP_EVENT_INFO_DEFINE(hid_consumer_event,
		      ENCODE(NRF_PROFILER_ARG_U16),
		      ENCODE("usage"),
		      profile_hid_consumer_event);

APP_EVENT_TYPE_DEFINE(hid_consumer_event,
		      log_hid_consumer_event,
		      &hid_consumer_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
