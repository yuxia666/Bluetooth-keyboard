/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * BLE HID Service — wraps bt_hids (keyboard + consumer), BAS, DIS.
 * Controls CAF ble_adv through module_resume_req / module_suspend_req.
 *
 * 对齐参考 keyboard 工程 src/ble_hid_service.c，并补强：
 *  - 加密完成（PEER_STATE_SECURED）+ 通知开启（CCC notify_handler）双门控，
 *    未满足时丢弃报告（要求 2）。
 *  - 与 USB 共用 keyboard_core / hid_scheduler / set_protocol_event / hid_led_event。
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <app_event_manager.h>

#define MODULE ble_hid
#include <caf/events/module_state_event.h>
#include <caf/events/module_suspend_event.h>
#include <caf/events/ble_common_event.h>
#include <caf/events/power_event.h>
#include <caf/events/power_manager_event.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/dis.h>
#include <bluetooth/services/hids.h>
#include <bluetooth/adv_prov.h>

#include <zephyr/logging/log.h>

#include "events/hid_report_to_send_event.h"
#include "events/hid_report_sent_event.h"
#include "events/set_protocol_event.h"
#include "events/hid_led_event.h"
#include "events/mode_event.h"
#include "hid_report_maps.h"

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_INF);

/* ================================================================= *
 *  Constants
 * ================================================================= */

#define BASE_USB_HID_SPEC_VERSION  0x0101

/* ================================================================= *
 *  HIDS Instance
 * ================================================================= */

BT_HIDS_DEF(hids_obj,
	    BLE_KBD_REPORT_SIZE,
	    BLE_CC_REPORT_SIZE);

/* ================================================================= *
 *  Advertising Provider: UUID16 (HIDS + BAS) — SD only, pairing mode
 * ================================================================= */

static int sd_uuid16_get_data(struct bt_data *d,
			      const struct bt_le_adv_prov_adv_state *state,
			      struct bt_le_adv_prov_feedback *fb)
{
	ARG_UNUSED(fb);

	if (!state->pairing_mode) {
		return -ENOENT;
	}

	static uint8_t data[] = {
		BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
		BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
	};

	d->type = BT_DATA_UUID16_ALL;
	d->data_len = sizeof(data);
	d->data = data;
	return 0;
}
BT_LE_ADV_PROV_SD_PROVIDER_REGISTER(uuid16_all, sd_uuid16_get_data);

/* ================================================================= *
 *  State
 * ================================================================= */

static struct bt_conn *_active_conn;
static bool _peer_boot_mode;
static bool _ble_active;

/* 要求 2：加密完成 + 通知开启 双门控 */
static bool _peer_secured;
static bool _peer_notify_on;

/* ================================================================= *
 *  Report Sending
 * ================================================================= */

static void inp_report_done(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);
	hid_report_sent_event_submit();
}

static void boot8_to_nkro(uint8_t *dst, const uint8_t *src)
{
	/* bt_hids adds Report ID from inp_rep->id — do NOT include it here.
	 * Output: 29 bytes (1 modifier + 28 bitmap), no report ID.
	 */
	dst[0] = src[0];          /* modifier */
	memset(&dst[1], 0, 28);   /* bitmap */

	for (int i = 2; i < 8; i++) {
		uint8_t usage = src[i];

		if (usage == 0x00 || usage > 0xDF) {
			continue;
		}
		dst[1 + (usage >> 3)] |= (uint8_t)(1 << (usage & 0x07));
	}
}

static void on_report_to_send(const struct hid_report_to_send_event *evt)
{
	int err;

	/* 门控：必须已连接。secured/notify 作为诊断信息（参考工程只查 _active_conn，
	 * 已绑定重连时 CCC 通知状态可能不重新触发，不宜硬阻塞）。
	 */
	if (!_active_conn) {
		LOG_WRN("BLE report dropped: no active conn, size=%u",
			evt->report_size);
		hid_report_sent_event_submit();
		return;
	}

	if (!_peer_secured || !_peer_notify_on) {
		LOG_WRN("BLE report (not secured/notify): conn=%p secured=%d notify=%d size=%u",
			(void *)_active_conn, _peer_secured, _peer_notify_on,
			evt->report_size);
	}

	if (evt->report_size == HID_CONSUMER_SIZE) {
		if (_peer_boot_mode) {
			hid_report_sent_event_submit();
			return;
		}
		/* bt_hids manages Report ID from inp_rep->id — send payload only (2 bytes) */
		err = bt_hids_inp_rep_send(&hids_obj, _active_conn,
					   BLE_INPUT_REP_CC_IDX,
					   evt->report, HID_CONSUMER_SIZE,
					   inp_report_done);
		if (err) {
			hid_report_sent_event_submit();
		}
	} else if (evt->report_size == HID_KBD_NKRO_SIZE) {
		/* bt_hids manages Report ID from inp_rep->id — send payload only (29 bytes) */
		err = bt_hids_inp_rep_send(&hids_obj, _active_conn,
					   BLE_INPUT_REP_KBD_IDX,
					   evt->report, HID_KBD_NKRO_SIZE,
					   inp_report_done);
		if (err) {
			hid_report_sent_event_submit();
		}
	} else if (evt->report_size == HID_KBD_BOOT_SIZE) {
		if (_peer_boot_mode) {
			err = bt_hids_boot_kb_inp_rep_send(&hids_obj, _active_conn,
							   evt->report,
							   HID_KBD_BOOT_SIZE,
							   inp_report_done);
		} else {
			uint8_t buf[HID_KBD_NKRO_SIZE];

			boot8_to_nkro(buf, evt->report);
			err = bt_hids_inp_rep_send(&hids_obj, _active_conn,
						   BLE_INPUT_REP_KBD_IDX,
						   buf, HID_KBD_NKRO_SIZE,
						   inp_report_done);
		}
		if (err) {
			hid_report_sent_event_submit();
		}
	}
}

/* ================================================================= *
 *  HIDS Callbacks
 * ================================================================= */

static void hids_pm_evt_handler(enum bt_hids_pm_evt evt,
				struct bt_conn *conn)
{
	_peer_boot_mode = (evt == BT_HIDS_PM_EVT_BOOT_MODE_ENTERED);

	set_protocol_event_submit(
		_peer_boot_mode ? HID_PROTOCOL_BOOT : HID_PROTOCOL_REPORT);

	LOG_INF("BLE %s mode entered",
		_peer_boot_mode ? "boot" : "report");
}

static void hids_outp_rep_handler(struct bt_hids_rep *rep,
				  struct bt_conn *conn,
				  bool write)
{
	if (!write || !rep || !rep->data || rep->size == 0) {
		return;
	}
	hid_led_event_submit(rep->data[0]);
}

static void hids_boot_kb_outp_rep_handler(struct bt_hids_rep *rep,
					  struct bt_conn *conn,
					  bool write)
{
	hids_outp_rep_handler(rep, conn, write);
}

/* 要求 2：CCC 通知开关回调 */
static void hids_notify_handler(enum bt_hids_notify_evt evt)
{
	_peer_notify_on = (evt == BT_HIDS_CCCD_EVT_NOTIFY_ENABLED);
	LOG_INF("BLE notify %s", _peer_notify_on ? "enabled" : "disabled");
}

/* ================================================================= *
 *  Connection Events
 * ================================================================= */

static void handle_peer_connected(struct bt_conn *conn)
{
	int err = bt_hids_connected(&hids_obj, conn);

	/* 即使 HIDS 上下文分配失败，蓝牙连接已建立，仍记录 _active_conn，
	 * 避免门控丢弃所有报告（bt_hids 发送时自会报错）。
	 */
	_active_conn = conn;
	_peer_boot_mode = false;
	_peer_secured = false;
	/* 注意：不重置 _peer_notify_on —— notify_handler 独立管理，
	 * 且 "notify enabled" 事件可能先于 connected 到达，重置会覆盖它。
	 */

	/* 方案 A：BLE 连接期间禁止系统挂起（防止休眠断开连接） */
	power_manager_restrict(MODULE_IDX(MODULE), POWER_MANAGER_LEVEL_ALIVE);

	if (err) {
		LOG_ERR("bt_hids_connected failed: %d (conn %p)", err, (void *)conn);
	} else {
		LOG_INF("BLE peer connected");
	}
}

static void handle_peer_secured(struct bt_conn *conn)
{
	_peer_secured = true;
	LOG_INF("BLE peer secured");
}

static void handle_peer_disconnected(struct bt_conn *conn)
{
	if (_active_conn == conn) {
		_active_conn = NULL;
	}

	_peer_secured = false;
	_peer_notify_on = false;
	bt_hids_disconnected(&hids_obj, conn);

	/* 断开后解除挂起限制（允许系统正常进入低功耗） */
	power_manager_restrict(MODULE_IDX(MODULE), POWER_MANAGER_LEVEL_MAX);
	LOG_INF("BLE peer disconnected");
}

/* ================================================================= *
 *  Init
 * ================================================================= */

static int hids_init(void)
{
	struct bt_hids_init_param init = { 0 };
	struct bt_hids_inp_rep *inp_rep;
	struct bt_hids_outp_feat_rep *outp_rep;

	init.rep_map.data = ble_report_map;
	init.rep_map.size = sizeof(ble_report_map);

	init.info.bcd_hid = BASE_USB_HID_SPEC_VERSION;
	init.info.b_country_code = 0x00;
	init.info.flags = (BT_HIDS_REMOTE_WAKE |
			   BT_HIDS_NORMALLY_CONNECTABLE);

	/* Input Report 0: Keyboard (29 bytes NKRO payload, bt_hids adds Report ID) */
	inp_rep = &init.inp_rep_group_init.reports[BLE_INPUT_REP_KBD_IDX];
	inp_rep->id = BLE_REPORT_ID_KEYBOARD;
	inp_rep->size = HID_KBD_NKRO_SIZE;
	inp_rep->handler = hids_notify_handler;   /* 要求 2：CCC 通知回调 */
	init.inp_rep_group_init.cnt++;

	/* Input Report 1: Consumer Control (2 bytes payload, bt_hids adds Report ID) */
	inp_rep = &init.inp_rep_group_init.reports[BLE_INPUT_REP_CC_IDX];
	inp_rep->id = BLE_REPORT_ID_CONSUMER;
	inp_rep->size = HID_CONSUMER_SIZE;
	inp_rep->handler = hids_notify_handler;   /* 要求 2：CCC 通知回调 */
	init.inp_rep_group_init.cnt++;

	/* Output Report 0: LED */
	outp_rep = &init.outp_rep_group_init.reports[0];
	outp_rep->id = BLE_REPORT_ID_KEYBOARD;
	outp_rep->size = 1;
	outp_rep->handler = hids_outp_rep_handler;
	init.outp_rep_group_init.cnt++;

	init.is_kb = true;
	init.boot_kb_outp_rep_handler = hids_boot_kb_outp_rep_handler;
	init.pm_evt_handler = hids_pm_evt_handler;

	int err = bt_hids_init(&hids_obj, &init);

	if (err) {
		LOG_ERR("bt_hids_init failed: %d", err);
	}
	return err;
}

/* ================================================================= *
 *  Lifecycle: Start / Stop (via CAF ble_adv module_resume/suspend)
 * ================================================================= */

static void ble_start(void)
{
	if (_ble_active) {
		return;
	}

	LOG_INF("BLE transport starting");

	struct module_resume_req_event *evt = new_module_resume_req_event();

	evt->sink_module_id = MODULE_ID(ble_adv);
	evt->src_module_id = MODULE_ID(MODULE);
	APP_EVENT_SUBMIT(evt);

	_ble_active = true;
	module_set_state(MODULE_STATE_READY);
}

static void ble_stop(void)
{
	/* 不因 _ble_active 跳过：唤醒后 ble_adv 可能自行恢复广播，
	 * 需要强制停止（幂等，重复 suspend 无害）。
	 */
	LOG_INF("BLE transport stopping");

	struct module_suspend_req_event *evt = new_module_suspend_req_event();

	evt->sink_module_id = MODULE_ID(ble_adv);
	evt->src_module_id = MODULE_ID(MODULE);
	APP_EVENT_SUBMIT(evt);

	/* Notify HIDS before clearing connection pointer */
	if (_active_conn) {
		bt_hids_disconnected(&hids_obj, _active_conn);
		bt_conn_disconnect(_active_conn,
				   BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		_active_conn = NULL;
	}

	_peer_secured = false;
	_peer_notify_on = false;
	_ble_active = false;
	module_set_state(MODULE_STATE_STANDBY);
}

/* ================================================================= *
 *  CAF Event Listener
 * ================================================================= */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *evt =
			cast_module_state_event(aeh);

		/* Registered before BLE to avoid GATT SC_LOAD guard.
		 * On main READY: init HIDS, stay stopped until mode_event selects BLE.
		 */
		if (check_state(evt, MODULE_ID(main), MODULE_STATE_READY)) {
			static bool initialized;

			__ASSERT_NO_MSG(!initialized);
			initialized = true;

			if (hids_init() == 0) {
				module_set_state(MODULE_STATE_STANDBY);
				LOG_INF("BLE HID service initialised (stopped)");
			} else {
				LOG_ERR("HIDS init failed");
			}
			return false;
		}

		return false;
	}

	if (is_ble_peer_event(aeh)) {
		const struct ble_peer_event *evt = cast_ble_peer_event(aeh);

		switch (evt->state) {
		case PEER_STATE_CONNECTED:
			handle_peer_connected(evt->id);
			break;
		case PEER_STATE_SECURED:
			handle_peer_secured(evt->id);
			break;
		case PEER_STATE_DISCONNECTED:
		case PEER_STATE_DISCONNECTING:
			handle_peer_disconnected(evt->id);
			break;
		default:
			break;
		}
		return false;
	}

	if (is_hid_report_to_send_event(aeh)) {
		on_report_to_send(cast_hid_report_to_send_event(aeh));
		return false;
	}

	if (is_mode_event(aeh)) {
		enum keyboard_mode mode = cast_mode_event(aeh)->mode;

		if (mode == KEYBOARD_MODE_BLE) {
			ble_start();
		} else {
			ble_stop();
		}
		return false;
	}

	if (is_power_down_event(aeh)) {
		ble_stop();          /* ble_stop() is idempotent — guarded by _ble_active */
		return false;
	}

	if (is_wake_up_event(aeh)) {
		/* 唤醒后：若当前模式非 BLE，确保 BLE 停止（防止 ble_adv 自行恢复广播） */
		if (mode_get_current() != KEYBOARD_MODE_BLE) {
			ble_stop();
		}
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, ble_peer_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_report_to_send_event);
APP_EVENT_SUBSCRIBE_EARLY(MODULE, mode_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
