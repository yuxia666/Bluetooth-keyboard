/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * HID Scheduler — 三通道发送流控
 *
 * 通道：USB-KBD（latest）、USB-CONSUMER（FIFO）、BLE-SHARED（键 latest + Consumer FIFO）
 * 键盘 latest：单槽 last-wins
 * Consumer FIFO：深度 32，满时丢队首 2 条（保持 press/release 配对）
 * 发送前等待通道 ready；in_flight 由 hid_report_sent_event 释放
 * scheduler 自持 current_mode，mode 切换时主动置通道 ready/清队列
 */

#include <string.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>

#include "events/hid_channel.h"
#include "events/hid_key_event.h"
#include "events/hid_consumer_event.h"
#include "events/hid_report_to_send_event.h"
#include "events/hid_report_sent_event.h"
#include "events/hid_channel_ready_event.h"
#include "events/mode_event.h"

#define MODULE hid_scheduler
#include <caf/events/module_state_event.h>

/* ── CC ring FIFO 深度（方案：32，旋钮 10 卡点 = 20 条） ── */
#define CC_FIFO_DEPTH  32

/* ── 通道状态 ── */
struct hid_channel_state {
	bool ready;
	bool in_flight;

	uint8_t kbd_report[29];
	uint8_t kbd_size;
	bool    kbd_pending;

	uint16_t cc_fifo[CC_FIFO_DEPTH];
	uint8_t  cc_head;
	uint8_t  cc_count;
};

static struct hid_channel_state _channels[HID_CHANNEL_COUNT];

static enum keyboard_mode _current_mode;

/* ── 工具 ── */

static void build_cc_report(uint8_t *out, uint16_t usage)
{
	out[0] = (uint8_t)(usage & 0xFF);
	out[1] = (uint8_t)(usage >> 8);
}

static void channel_reset(struct hid_channel_state *ch)
{
	memset(ch, 0, sizeof(*ch));
}

static struct hid_channel_state *channel_get(enum hid_channel ch)
{
	return &_channels[(unsigned)ch];
}

/* 判断某通道在当前模式下是否启用 */
static bool channel_enabled_in_mode(enum hid_channel ch,
				    enum keyboard_mode mode)
{
	if (mode == KEYBOARD_MODE_USB && ch == HID_CHANNEL_USB_KBD) {
		return true;
	}
	if (mode == KEYBOARD_MODE_USB && ch == HID_CHANNEL_USB_CONSUMER) {
		return true;
	}
	if (mode == KEYBOARD_MODE_BLE && ch == HID_CHANNEL_BLE_SHARED) {
		return true;
	}
	return false;
}

/* ── flush ── */

static void try_flush_channel(struct hid_channel_state *ch,
			      enum hid_channel channel_id)
{
	if (ch->in_flight || !ch->ready) {
		return;
	}

	/* 键盘 priority */
	if (ch->kbd_pending) {
		hid_report_to_send_event_submit(channel_id,
						ch->kbd_report, ch->kbd_size);
		ch->kbd_pending = false;
		ch->in_flight = true;
		return;
	}

	/* consumer FIFO */
	if (ch->cc_count > 0) {
		uint8_t report[2];

		build_cc_report(report, ch->cc_fifo[ch->cc_head]);
		ch->cc_head = (ch->cc_head + 1) % CC_FIFO_DEPTH;
		ch->cc_count--;

		hid_report_to_send_event_submit(channel_id, report, 2);
		ch->in_flight = true;
	}
}

static void try_flush_all(void)
{
	/* 按当前模式只 flush 启用通道 */
	for (int i = 0; i < HID_CHANNEL_COUNT; i++) {
		struct hid_channel_state *ch = &_channels[i];

		if (channel_enabled_in_mode((enum hid_channel)i, _current_mode)) {
			try_flush_channel(ch, (enum hid_channel)i);
		}
	}
}

/* ── 事件处理 ── */

static void on_hid_key_event(const struct hid_key_event *evt)
{
	enum hid_channel ch_id =
		(_current_mode == KEYBOARD_MODE_BLE) ? HID_CHANNEL_BLE_SHARED :
		(_current_mode == KEYBOARD_MODE_USB) ? HID_CHANNEL_USB_KBD :
		HID_CHANNEL_COUNT;

	if (ch_id == HID_CHANNEL_COUNT) {
		return;	/* 2.4G：丢弃 */
	}

	struct hid_channel_state *ch = channel_get(ch_id);

	memcpy(ch->kbd_report, evt->report, evt->report_size);
	ch->kbd_size = evt->report_size;
	ch->kbd_pending = true;
	try_flush_channel(ch, ch_id);
}

static void on_hid_consumer_event(const struct hid_consumer_event *evt)
{
	enum hid_channel ch_id =
		(_current_mode == KEYBOARD_MODE_BLE) ? HID_CHANNEL_BLE_SHARED :
		(_current_mode == KEYBOARD_MODE_USB) ? HID_CHANNEL_USB_CONSUMER :
		HID_CHANNEL_COUNT;

	if (ch_id == HID_CHANNEL_COUNT) {
		return;	/* 2.4G：丢弃 */
	}

	struct hid_channel_state *ch = channel_get(ch_id);

	/* 满：丢队首 2 条（保持 press/release 配对） */
	if (ch->cc_count + 2 > CC_FIFO_DEPTH) {
		ch->cc_head = (ch->cc_head + 2) % CC_FIFO_DEPTH;
		ch->cc_count -= 2;
	}

	uint8_t idx = (ch->cc_head + ch->cc_count) % CC_FIFO_DEPTH;

	ch->cc_fifo[idx] = evt->usage;
	ch->cc_count++;
	try_flush_channel(ch, ch_id);
}

static void on_hid_report_sent(const struct hid_report_sent_event *evt)
{
	enum hid_channel ch_id = evt->channel;

	if ((unsigned)ch_id >= HID_CHANNEL_COUNT) {
		return;
	}

	struct hid_channel_state *ch = channel_get(ch_id);

	ch->in_flight = false;
	try_flush_channel(ch, ch_id);
}

static void on_hid_channel_ready(const struct hid_channel_ready_event *evt)
{
	if ((unsigned)evt->channel >= HID_CHANNEL_COUNT) {
		return;
	}

	struct hid_channel_state *ch = channel_get(evt->channel);

	ch->ready = evt->ready;
	if (!evt->ready) {
		/* 失联：强制释放 in_flight，防止队列冻结 */
		ch->in_flight = false;
	}
	try_flush_channel(ch, evt->channel);
}

static void on_mode_event(const struct mode_event *evt)
{
	_current_mode = evt->mode;

	/* mode 切换：主动置通道 ready/清队列（方案：原子一步） */
	for (int i = 0; i < HID_CHANNEL_COUNT; i++) {
		struct hid_channel_state *ch = &_channels[i];

		if (channel_enabled_in_mode((enum hid_channel)i, _current_mode)) {
			/* 新启用通道：置 ready=true（transport ready 事件做增量修正） */
			ch->ready = true;
			/* 清残留队列 */
			ch->kbd_pending = false;
			ch->cc_count = 0;
			ch->cc_head = 0;
			ch->in_flight = false;
		} else {
			/* 禁用通道：清空并置 not ready */
			channel_reset(ch);
			ch->ready = false;
		}
	}

	try_flush_all();
}

/* ── CAF ── */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *evt = cast_module_state_event(aeh);

		if (evt->state == MODULE_STATE_READY &&
		    evt->module_id == MODULE_ID(main)) {
			for (int i = 0; i < HID_CHANNEL_COUNT; i++) {
				channel_reset(&_channels[i]);
			}
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
		on_hid_report_sent(cast_hid_report_sent_event(aeh));
		return false;
	}

	if (is_hid_channel_ready_event(aeh)) {
		on_hid_channel_ready(cast_hid_channel_ready_event(aeh));
		return false;
	}

	if (is_mode_event(aeh)) {
		on_mode_event(cast_mode_event(aeh));
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_key_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_consumer_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, hid_report_sent_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_channel_ready_event);
APP_EVENT_SUBSCRIBE(MODULE, mode_event);
