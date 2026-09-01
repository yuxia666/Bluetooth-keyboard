/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Keyboard core — subscribes to button_event and encoder_event,
 * manages keystate (29-byte bitset), builds HID reports (boot + NKRO),
 * and submits hid_key_event / hid_consumer_event.
 *
 * 对齐参考 keyboard 工程 src/keyboard_core.c。
 * 当前阶段无 BLE/USB 传输层，HID 报告由 hid_report_log 消费者打印。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>
#include <caf/events/button_event.h>
#include <caf/key_id.h>

#include "events/encoder_event.h"
#include "events/hid_key_event.h"
#include "events/hid_consumer_event.h"
#include "events/set_protocol_event.h"
#include "events/bitmap_cfg_event.h"
#include "events/mode_event.h"
#include "events/funckey_report_event.h"

#include <string.h>

#define MODULE keyboard_core
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(MODULE, CONFIG_LOG_DEFAULT_LEVEL);

/* ================================================================= *
 *  Constants
 * ================================================================= */

#define CONSUMER_MUTE         0xE2
#define CONSUMER_VOL_UP       0xE9
#define CONSUMER_VOL_DOWN     0xEA

/* keystate */
#define KS_SIZE               29
#define KS_MODIFIER_BYTE      28

/* boot protocol */
#define BOOT_SLOTS            6
#define ERROR_ROLL_OVER       0x01

/* NKRO: modifier(1) + bitmap(28) = 29 bytes, no report ID */

/* ================================================================= *
 *  Keymap
 * ================================================================= */

static const uint16_t _keymap[6][4] = {
	/* { COL0      COL1      COL2      COL3   } */
	{    0x0000,   0x0000,   0x0000,   0x10E2 },  /* ROW0: ENC Btn -> Mute */
	{    0x0053,   0x0055,   0x0054,   0x0056 },  /* ROW1: NumLk, *, /, -  */
	{    0x005F,   0x0060,   0x0061,   0x0000 },  /* ROW2: 7, 8, 9        */
	{    0x005C,   0x005D,   0x005E,   0x0057 },  /* ROW3: 4, 5, 6, +     */
	{    0x0059,   0x005A,   0x005B,   0x0000 },  /* ROW4: 1, 2, 3        */
	{    0x0062,   0x0063,   0x0000,   0x0058 },  /* ROW5: 0, ., Enter    */
};

static uint16_t keymap_lookup(uint16_t caf_key_id)
{
	uint8_t col = KEY_COL(caf_key_id);
	uint8_t row = KEY_ROW(caf_key_id);

	if (col >= 4 || row >= 6) {
		return 0x0000;
	}
	return _keymap[row][col];
}

static bool keymap_is_consumer(uint16_t key_id)
{
	return (key_id >> 8) == 0x10;
}

static uint8_t keymap_keyboard_usage(uint16_t key_id)
{
	return (uint8_t)(key_id & 0xFF);
}

static uint16_t keymap_consumer_usage(uint16_t key_id)
{
	return (uint16_t)(key_id & 0xFF);
}

/* ================================================================= *
 *  Key State (29-byte bitset)
 * ================================================================= */

static uint8_t _ks[KS_SIZE];
static uint8_t _ks_count;

static inline bool _ks_is_modifier(uint8_t id)
{
	return (id >= 0xE0) && (id <= 0xE7);
}

static void keystate_init(void)
{
	memset(_ks, 0, sizeof(_ks));
	_ks_count = 0;
}

static bool keystate_set(uint8_t id)
{
	if (id >= 0xE8) {
		return false;
	}
	uint8_t byte = id >> 3;
	uint8_t mask = 1 << (id & 0x07);

	if (_ks[byte] & mask) {
		return false;
	}
	_ks[byte] |= mask;
	if (!_ks_is_modifier(id)) {
		_ks_count++;
	}
	return true;
}

static bool keystate_clear(uint8_t id)
{
	if (id >= 0xE8) {
		return false;
	}
	uint8_t byte = id >> 3;
	uint8_t mask = 1 << (id & 0x07);

	if (!(_ks[byte] & mask)) {
		return false;
	}
	_ks[byte] &= ~mask;
	if (!_ks_is_modifier(id)) {
		_ks_count--;
	}
	return true;
}

static uint8_t keystate_count(void)
{
	return _ks_count;
}

static uint8_t keystate_modifier_byte(void)
{
	return _ks[KS_MODIFIER_BYTE];
}

static uint8_t keystate_collect_keys(uint8_t *out, uint8_t max)
{
	uint8_t n = 0;

	for (uint16_t u = 0x04; u <= 0xDF && n < max; u++) {
		if ((_ks[u >> 3] >> (u & 0x07)) & 1) {
			*out++ = (uint8_t)u;
			n++;
		}
	}
	return n;
}

/* ================================================================= *
 *  HID Report Builder
 * ================================================================= */

static void hid_build_boot_report(uint8_t *out)
{
	uint8_t count = keystate_count();

	out[0] = keystate_modifier_byte();
	out[1] = 0x00;

	if (count == 0) {
		memset(&out[2], 0, BOOT_SLOTS);
	} else if (count > BOOT_SLOTS) {
		memset(&out[2], ERROR_ROLL_OVER, BOOT_SLOTS);
	} else {
		uint8_t usages[BOOT_SLOTS];

		memset(usages, 0, sizeof(usages));
		keystate_collect_keys(usages, BOOT_SLOTS);
		memcpy(&out[2], usages, BOOT_SLOTS);
	}
}

static void hid_build_nkro_report(uint8_t *out)
{
	out[0] = keystate_modifier_byte();
	memcpy(&out[1], _ks, KS_SIZE - 1);
}

/* ================================================================= *
 *  Functional Key Bitmap (29-byte bitset, same format as keystate)
 * ================================================================= */

static uint8_t _functional_bitmap[FUNCKEY_BITMAP_SIZE];

static bool is_functional_key(uint8_t usage)
{
	uint8_t byte = usage >> 3;
	uint8_t mask = 1 << (usage & 0x07);

	return (_functional_bitmap[byte] & mask) != 0;
}

/* ================================================================= *
 *  Report submission
 * ================================================================= */

/* HID spec §7.2.6: default is Report Protocol */
static uint8_t _protocol = HID_PROTOCOL_REPORT;
static bool _suspended;

static void submit_report(void)
{
	uint8_t report[HID_NKRO_REPORT_SIZE];

	if (_protocol == HID_PROTOCOL_REPORT) {
		hid_build_nkro_report(report);
		hid_key_event_submit(HID_PROTOCOL_REPORT, report, HID_NKRO_REPORT_SIZE);
	} else {
		hid_build_boot_report(report);
		hid_key_event_submit(HID_PROTOCOL_BOOT, report, HID_BOOT_REPORT_SIZE);
	}
}

static void submit_funckey_report(void)
{
	uint8_t bitmap[FUNCKEY_REPORT_SIZE];

	/* Keystate order: [00-DF] [E0-E7]
	 * Protocol order: [E0-E7] [00-DF]
	 */
	bitmap[0] = keystate_modifier_byte();
	memcpy(bitmap + 1, _ks, FUNCKEY_REPORT_SIZE - 1);
	funckey_report_event_submit(bitmap);
}

/* ================================================================= *
 *  Event handlers
 * ================================================================= */

static void handle_button(const struct button_event *btn)
{
	uint16_t key_id = keymap_lookup(btn->key_id);

	if (key_id == 0x0000) {
		return;
	}

	if (keymap_is_consumer(key_id)) {
		hid_consumer_event_submit(btn->pressed ? keymap_consumer_usage(key_id) : 0x0000);
	} else {
		uint8_t usage = keymap_keyboard_usage(key_id);

		if (is_functional_key(usage)) {
			/* Functional key: track keystate but go through
			 * funckey_report_event instead of HID report.
			 */
			bool changed = btn->pressed ? keystate_set(usage)
						   : keystate_clear(usage);
			if (changed) {
				submit_funckey_report();
			}
		} else {
			/* Normal key: standard HID report flow */
			bool changed = btn->pressed ? keystate_set(usage)
						   : keystate_clear(usage);
			if (changed) {
				submit_report();
			}
		}
	}
}

static void handle_encoder(const struct encoder_event *enc)
{
	uint16_t usage = (enc->steps > 0) ? CONSUMER_VOL_UP : CONSUMER_VOL_DOWN;
	uint8_t steps = (enc->steps > 0) ? (uint8_t)enc->steps : (uint8_t)(-enc->steps);

	for (uint8_t i = 0; i < steps; i++) {
		hid_consumer_event_submit(usage);
		hid_consumer_event_submit(0x0000);
	}
}

/* ================================================================= *
 *  Init
 * ================================================================= */

static int init(void)
{
	keystate_init();
	memset(_functional_bitmap, 0, sizeof(_functional_bitmap));
	_suspended = false;
	module_set_state(MODULE_STATE_READY);
	LOG_INF("Keyboard core initialised (report protocol)");
	return 0;
}

/* ================================================================= *
 *  CAF event listener
 * ================================================================= */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *evt = cast_module_state_event(aeh);

		if (evt->state == MODULE_STATE_READY &&
		    evt->module_id == MODULE_ID(main)) {
			init();
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (_suspended) { return false; }
		_suspended = true;
		module_set_state(MODULE_STATE_OFF);
		return false;
	}

	if (is_wake_up_event(aeh)) {
		_suspended = false;
		keystate_init();
		module_set_state(MODULE_STATE_READY);
		return false;
	}

	if (_suspended) {
		return false;
	}

	if (is_button_event(aeh)) {
		handle_button(cast_button_event(aeh));
		return false;
	}

	if (is_encoder_event(aeh)) {
		handle_encoder(cast_encoder_event(aeh));
		return false;
	}

	if (is_mode_event(aeh)) {
		const struct mode_event *evt = cast_mode_event(aeh);

		/* 切到 USB/BLE 时重发当前 keystate 快照（对齐方案 §10） */
		if (evt->mode == KEYBOARD_MODE_USB ||
		    evt->mode == KEYBOARD_MODE_BLE) {
			submit_report();
		}
		return false;
	}

	if (is_set_protocol_event(aeh)) {
		const struct set_protocol_event *evt = cast_set_protocol_event(aeh);

		_protocol = evt->protocol;
		LOG_INF("Protocol switched to %s",
			_protocol == HID_PROTOCOL_REPORT ? "report" : "boot");
		return false;
	}

	if (is_bitmap_cfg_event(aeh)) {
		const struct bitmap_cfg_event *evt = cast_bitmap_cfg_event(aeh);

		/* Protocol byte order: [E0-E7] [00-07] ... [D8-DF]
		 * Keystate byte order: [00-07] ... [D8-DF] [E0-E7]
		 * Convert by rotating the E0-E7 byte from index 0 to index 28.
		 */
		uint8_t modifier_byte = evt->usage_bitmap[0];

		memmove(_functional_bitmap, evt->usage_bitmap + 1,
			FUNCKEY_BITMAP_SIZE - 1);
		_functional_bitmap[FUNCKEY_BITMAP_SIZE - 1] = modifier_byte;

		LOG_INF("Functional key bitmap updated (reordered)");
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
APP_EVENT_SUBSCRIBE(MODULE, encoder_event);
APP_EVENT_SUBSCRIBE(MODULE, mode_event);
APP_EVENT_SUBSCRIBE(MODULE, set_protocol_event);
APP_EVENT_SUBSCRIBE(MODULE, bitmap_cfg_event);
