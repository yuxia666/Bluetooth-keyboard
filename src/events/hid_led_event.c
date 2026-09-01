/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#include "events/hid_led_event.h"

static void log_hid_led_event(const struct app_event_header *aeh)
{
	const struct hid_led_event *evt = cast_hid_led_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "leds=0x%02x", evt->led_state);
}

static void profile_hid_led_event(struct log_event_buf *buf,
				  const struct app_event_header *aeh)
{
	const struct hid_led_event *evt = cast_hid_led_event(aeh);

	nrf_profiler_log_encode_uint8(buf, evt->led_state);
}

APP_EVENT_INFO_DEFINE(hid_led_event,
		      ENCODE(NRF_PROFILER_ARG_U8),
		      ENCODE("led_state"),
		      profile_hid_led_event);

APP_EVENT_TYPE_DEFINE(hid_led_event,
		      log_hid_led_event,
		      &hid_led_event_info,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
