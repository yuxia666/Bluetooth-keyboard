/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "events/theme_rgb_update_event.h"

static void log_theme_rgb_update_event(const struct app_event_header *aeh)
{
	const struct theme_rgb_update_event *event =
		cast_theme_rgb_update_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "r=%u g=%u b=%u",
			      event->theme_color.r,
			      event->theme_color.g,
			      event->theme_color.b);
}

static void profile_theme_rgb_update_event(struct log_event_buf *buf,
					   const struct app_event_header *aeh)
{
	const struct theme_rgb_update_event *event =
		cast_theme_rgb_update_event(aeh);

	nrf_profiler_log_encode_uint8(buf, event->theme_color.r);
	nrf_profiler_log_encode_uint8(buf, event->theme_color.g);
	nrf_profiler_log_encode_uint8(buf, event->theme_color.b);
}

APP_EVENT_INFO_DEFINE(theme_rgb_update_event,
		      ENCODE(NRF_PROFILER_ARG_U8, NRF_PROFILER_ARG_U8,
			     NRF_PROFILER_ARG_U8),
		      ENCODE("r", "g", "b"),
		      profile_theme_rgb_update_event);

APP_EVENT_TYPE_DEFINE(theme_rgb_update_event,
		      log_theme_rgb_update_event,
		      &theme_rgb_update_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
