/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/battery_event.h"

static void log_battery_event(const struct app_event_header *aeh)
{
	const struct battery_event *event = cast_battery_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "lvl=%u st=%u",
			      event->level, event->state);
}

static void profile_battery_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
	const struct battery_event *event = cast_battery_event(aeh);

	nrf_profiler_log_encode_uint8(buf, event->level);
	nrf_profiler_log_encode_uint8(buf, event->state);
}

APP_EVENT_INFO_DEFINE(battery_event,
		  ENCODE(NRF_PROFILER_ARG_U8, NRF_PROFILER_ARG_U8),
		  ENCODE("level", "state"),
		  profile_battery_event);

APP_EVENT_TYPE_DEFINE(battery_event,
		  log_battery_event,
		  &battery_event_info,
		  APP_EVENT_FLAGS_CREATE(
		APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
