/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "events/led_strip_en_event.h"

static void log_led_strip_en_event(const struct app_event_header *aeh)
{
	const struct led_strip_en_event *event =
		cast_led_strip_en_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "enabled=%u", event->enabled);
}

static void profile_led_strip_en_event(struct log_event_buf *buf,
				       const struct app_event_header *aeh)
{
	const struct led_strip_en_event *event =
		cast_led_strip_en_event(aeh);

	nrf_profiler_log_encode_uint8(buf, event->enabled ? 1 : 0);
}

APP_EVENT_INFO_DEFINE(led_strip_en_event,
		      ENCODE(NRF_PROFILER_ARG_U8),
		      ENCODE("enabled"),
		      profile_led_strip_en_event);

APP_EVENT_TYPE_DEFINE(led_strip_en_event,
		      log_led_strip_en_event,
		      &led_strip_en_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
