/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * HID Scheduler — FIFO rate limiter between keyboard_core and USB transport.
 *
 * Keyboard reports: last-wins (single slot overwritten by latest).
 * Consumer reports: 16-deep ring FIFO, each step preserved.
 * Send priority: keyboard > consumer.
 * Backpressure: waits for hid_report_sent_event before next dequeue.
 *
 * 对齐参考 keyboard 工程 src/hid_scheduler.c。
 */

#include <string.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>

#include "events/hid_key_event.h"
#include "events/hid_consumer_event.h"
#include "events/hid_report_to_send_event.h"
#include "events/hid_report_sent_event.h"
#include "events/mode_event.h"

#define MODULE hid_scheduler
#include <caf/events/module_state_event.h>

/* ── CC ring FIFO ── */
#define CC_FIFO_DEPTH  16

/* ── state ── */
static uint8_t  _kbd_report[29];
static uint8_t  _kbd_size;
static bool     _kbd_pending;

static uint16_t _cc_fifo[CC_FIFO_DEPTH];
static uint8_t  _cc_head;
static uint8_t  _cc_count;

static bool _sending;

/* ── flush ── */

static void build_cc_report(uint8_t *out, uint16_t usage)
{
	out[0] = (uint8_t)(usage & 0xFF);
	out[1] = (uint8_t)(usage >> 8);
}

static void try_flush(void)
{
	if (_sending) {
		return;
	}

	/* keyboard priority */
	if (_kbd_pending) {
		hid_report_to_send_event_submit(_kbd_report, _kbd_size);
		_kbd_pending = false;
		_sending = true;
		return;
	}

	/* consumer ring FIFO */
	if (_cc_count > 0) {
		uint8_t report[2];

		build_cc_report(report, _cc_fifo[_cc_head]);
		_cc_head = (_cc_head + 1) % CC_FIFO_DEPTH;
		_cc_count--;

		hid_report_to_send_event_submit(report, 2);
		_sending = true;
	}
}

/* ── event handlers ── */

static void on_hid_key_event(const struct hid_key_event *evt)
{
	memcpy(_kbd_report, evt->report, evt->report_size);
	_kbd_size = evt->report_size;
	_kbd_pending = true;
	try_flush();
}

static void on_hid_consumer_event(const struct hid_consumer_event *evt)
{
	if (_cc_count >= CC_FIFO_DEPTH) {
		return;
	}
	uint8_t idx = (_cc_head + _cc_count) % CC_FIFO_DEPTH;

	_cc_fifo[idx] = evt->usage;
	_cc_count++;
	try_flush();
}

static void on_hid_report_sent(void)
{
	_sending = false;
	try_flush();
}

/* ── CAF ── */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *evt = cast_module_state_event(aeh);

		if (evt->state == MODULE_STATE_READY &&
		    evt->module_id == MODULE_ID(main)) {
			_kbd_pending = false;
			_cc_count = 0;
			_sending = false;
			module_set_state(MODULE_STATE_READY);
		}
		return false;
	}

	if (is_hid_key_event(aeh)) {
		on_hid_key_event(cast_hid_key_event(aeh));
		return false;
	}

	if (is_hid_consumer_event(aeh)) {
		on_hid_consumer_event(cast_hid_consumer_event(aeh));
		return false;
	}

	if (is_hid_report_sent_event(aeh)) {
		on_hid_report_sent();
		return false;
	}

	if (is_mode_event(aeh)) {
		_kbd_pending = false;
		_cc_count = 0;
		_sending = false;
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_key_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_consumer_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, hid_report_sent_event);
APP_EVENT_SUBSCRIBE(MODULE, mode_event);
