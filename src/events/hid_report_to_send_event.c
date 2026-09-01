/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_report_to_send_event.h"

static void log_hid_report_to_send_event(const struct app_event_header *aeh)
{
	const struct hid_report_to_send_event *evt =
		cast_hid_report_to_send_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "ch=%u size=%u",
			      (unsigned)evt->channel, evt->report_size);
}

static void profile_hid_report_to_send_event(struct log_event_buf *buf,
					     const struct app_event_header *aeh)
{
	const struct hid_report_to_send_event *evt =
		cast_hid_report_to_send_event(aeh);

	nrf_profiler_log_encode_uint8(buf, evt->channel);
	nrf_profiler_log_encode_uint8(buf, evt->report_size);
}

APP_EVENT_INFO_DEFINE(hid_report_to_send_event,
		      ENCODE(NRF_PROFILER_ARG_U8, NRF_PROFILER_ARG_U8),
		      ENCODE("channel", "report_size"),
		      profile_hid_report_to_send_event);

APP_EVENT_TYPE_DEFINE(hid_report_to_send_event,
		      log_hid_report_to_send_event,
		      &hid_report_to_send_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
