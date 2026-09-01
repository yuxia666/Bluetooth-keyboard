/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "events/hid_channel_ready_event.h"

static void log_hid_channel_ready_event(const struct app_event_header *aeh)
{
	const struct hid_channel_ready_event *evt =
		cast_hid_channel_ready_event(aeh);

	APP_EVENT_MANAGER_LOG(aeh, "channel=%u ready=%u",
			      (unsigned)evt->channel, evt->ready);
}

APP_EVENT_TYPE_DEFINE(hid_channel_ready_event,
		      log_hid_channel_ready_event,
		      NULL,
		      APP_EVENT_FLAGS_CREATE(
			APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE));
