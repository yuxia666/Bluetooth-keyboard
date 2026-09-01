/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * USB HID Transport — manages both hid_kbd and hid_consumer devices,
 * plus CDC ACM (third composite interface, placeholder).
 *
 * 对齐参考 keyboard 工程 src/usb_hid_transport.c：
 *  - 复合设备：keyboard HID + consumer HID + CDC ACM
 *  - Boot Protocol：SET_PROTOCOL → set_protocol_event
 *  - LED output report → hid_led_event
 *  - mode_event：USB 档 usbd_enable，其它档 usbd_disable
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <app_event_manager.h>

#define MODULE usb_hid
#include <caf/events/module_state_event.h>
#include <caf/events/power_event.h>

#include <zephyr/device.h>
#include <zephyr/drivers/usb/udc_buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/class/hid.h>

#include "events/hid_report_to_send_event.h"
#include "events/hid_report_sent_event.h"
#include "events/set_protocol_event.h"
#include "events/hid_led_event.h"
#include "events/mode_event.h"
#include "hid_report_maps.h"

LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_INF);

/* ================================================================= *
 *  USB Device Descriptors
 * ================================================================= */

#define USB_VID  0x2FE3
#define USB_PID  0x0001

USBD_DEVICE_DEFINE(usbd_ctx,
		   DEVICE_DT_GET(DT_NODELABEL(usbd)),
		   USB_VID, USB_PID);

USBD_DESC_LANG_DEFINE(usbd_lang);
USBD_DESC_MANUFACTURER_DEFINE(usbd_mfr, "atguigu");
USBD_DESC_PRODUCT_DEFINE(usbd_product, "Mini Keyboard");
USBD_DESC_CONFIG_DEFINE(usbd_fs_cfg, "FS Configuration");
USBD_CONFIGURATION_DEFINE(usbd_fs_config, 0, 100, &usbd_fs_cfg);

/* Report descriptors moved to inc/hid_report_maps.h */

/* ================================================================= *
 *  Per-device State
 * ================================================================= */

#define KBD_REPORT_MAX   HID_KBD_NKRO_SIZE
#define CONSUMER_REPORT  HID_CONSUMER_SIZE

static const struct device *_kbd_dev;
static const struct device *_consumer_dev;
static bool _kbd_iface_ready;
static bool _consumer_iface_ready;
static bool _kbd_in_flight;
static bool _consumer_in_flight;
static bool _powered_down;

UDC_STATIC_BUF_DEFINE(_kbd_tx, KBD_REPORT_MAX);
UDC_STATIC_BUF_DEFINE(_consumer_tx, CONSUMER_REPORT);

/* ================================================================= *
 *  Keyboard HID Callbacks
 * ================================================================= */

static void kbd_iface_ready(const struct device *dev, bool ready)
{
	_kbd_iface_ready = ready;
	if (!ready) { _kbd_in_flight = false; }
	LOG_INF("KBD interface %s", ready ? "ready" : "suspended");
}

static int kbd_get_report(const struct device *dev, const uint8_t type,
			  const uint8_t id, const uint16_t len, uint8_t *const buf)
{
	return -ENOTSUP;
}

static void kbd_handle_led(const uint8_t *buf, uint16_t len)
{
	if (buf == NULL || len == 0) { return; }
	uint8_t led = (len >= 2) ? buf[1] : buf[0]; /* skip report ID if present */

	hid_led_event_submit(led);
}

static int kbd_set_report(const struct device *dev, const uint8_t type,
			  const uint8_t id, const uint16_t len,
			  const uint8_t *const buf)
{
	if (type == HID_REPORT_TYPE_OUTPUT) { kbd_handle_led(buf, len); }
	return 0;
}

static void kbd_set_protocol(const struct device *dev, uint8_t proto)
{
	LOG_INF("Host set protocol: %s",
		proto == HID_PROTOCOL_BOOT ? "boot" : "report");
	set_protocol_event_submit(proto == HID_PROTOCOL_BOOT
				  ? HID_PROTOCOL_BOOT : HID_PROTOCOL_REPORT);
}

static void kbd_input_report_done(const struct device *dev,
				  const uint8_t *const report)
{
	_kbd_in_flight = false;
	hid_report_sent_event_submit();
}

static void kbd_output_report(const struct device *dev, const uint16_t len,
			      const uint8_t *const buf)
{
	kbd_handle_led(buf, len);
}

static struct hid_device_ops _kbd_ops = {
	.iface_ready       = kbd_iface_ready,
	.get_report        = kbd_get_report,
	.set_report        = kbd_set_report,
	.set_protocol      = kbd_set_protocol,
	.input_report_done = kbd_input_report_done,
	.output_report     = kbd_output_report,
};

/* ================================================================= *
 *  Consumer Control HID Callbacks
 * ================================================================= */

static void consumer_iface_ready(const struct device *dev, bool ready)
{
	_consumer_iface_ready = ready;
	if (!ready) { _consumer_in_flight = false; }
	LOG_INF("Consumer interface %s", ready ? "ready" : "suspended");
}

static int consumer_get_report(const struct device *dev, const uint8_t type,
			       const uint8_t id, const uint16_t len,
			       uint8_t *const buf)
{
	return -ENOTSUP;
}

static int consumer_set_report(const struct device *dev, const uint8_t type,
			       const uint8_t id, const uint16_t len,
			       const uint8_t *const buf)
{
	return -ENOTSUP;
}

static void consumer_input_report_done(const struct device *dev,
				       const uint8_t *const report)
{
	_consumer_in_flight = false;
	hid_report_sent_event_submit();
}

static void consumer_output_report(const struct device *dev, const uint16_t len,
				   const uint8_t *const buf)
{
}

static struct hid_device_ops _consumer_ops = {
	.iface_ready       = consumer_iface_ready,
	.get_report        = consumer_get_report,
	.set_report        = consumer_set_report,
	.set_protocol      = NULL,
	.input_report_done = consumer_input_report_done,
	.output_report     = consumer_output_report,
};

/* ================================================================= *
 *  USBD Message Callback
 * ================================================================= */

static void usbd_msg_cb(struct usbd_context *const ctx,
			const struct usbd_msg *const msg)
{
	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable USB after VBUS");
			}
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable USB after VBUS removal");
			}
		}
	}
}

/* ================================================================= *
 *  USBD Initialization
 * ================================================================= */

static int usbd_setup(void)
{
	int err;

	err = usbd_add_descriptor(&usbd_ctx, &usbd_lang);
	if (err) { LOG_ERR("lang desc: %d", err); return err; }
	err = usbd_add_descriptor(&usbd_ctx, &usbd_mfr);
	if (err) { LOG_ERR("mfr desc: %d", err); return err; }
	err = usbd_add_descriptor(&usbd_ctx, &usbd_product);
	if (err) { LOG_ERR("product desc: %d", err); return err; }

	err = usbd_add_configuration(&usbd_ctx, USBD_SPEED_FS, &usbd_fs_config);
	if (err) { LOG_ERR("FS config: %d", err); return err; }

	err = usbd_register_all_classes(&usbd_ctx, USBD_SPEED_FS, 1, NULL);
	if (err) { LOG_ERR("register classes: %d", err); return err; }

	usbd_device_set_code_triple(&usbd_ctx, USBD_SPEED_FS, 0, 0, 0);

	err = usbd_msg_register_cb(&usbd_ctx, usbd_msg_cb);
	if (err) { LOG_ERR("msg cb: %d", err); return err; }

	err = usbd_init(&usbd_ctx);
	if (err) { LOG_ERR("usbd_init: %d", err); return err; }

	/*
	 * nRF52840 的 UDC 驱动无条件上报 can_detect_vbus=true，
	 * 但 nRF52840 无 USBREG 外设（NRF_POWER_HAS_USBREG=0），
	 * nrfx_power_usbevt_enable() 为空实现，VBUS 事件不会产生，
	 * usbd_enable() 永远不会被自动调用。
	 * 因此必须无条件主动使能 USB（nRF52840 由 usbd_enable 直接驱动外设）。
	 */
	err = usbd_enable(&usbd_ctx);
	if (err) { LOG_ERR("usbd_enable: %d", err); return err; }

	return 0;
}

/* ================================================================= *
 *  HID Device Registration
 * ================================================================= */

static int register_hid_devices(void)
{
	int err;

	_kbd_dev = DEVICE_DT_GET(DT_NODELABEL(hid_kbd));
	if (!device_is_ready(_kbd_dev)) {
		LOG_ERR("hid_kbd device not ready");
		return -ENODEV;
	}
	err = hid_device_register(_kbd_dev, usb_kbd_report_desc,
				  sizeof(usb_kbd_report_desc), &_kbd_ops);
	if (err) {
		LOG_ERR("hid_kbd register failed: %d", err);
		return err;
	}

	_consumer_dev = DEVICE_DT_GET(DT_NODELABEL(hid_consumer));
	if (!device_is_ready(_consumer_dev)) {
		LOG_ERR("hid_consumer device not ready");
		return -ENODEV;
	}
	err = hid_device_register(_consumer_dev, usb_consumer_report_desc,
				  sizeof(usb_consumer_report_desc), &_consumer_ops);
	if (err) {
		LOG_ERR("hid_consumer register failed: %d", err);
		return err;
	}

	return 0;
}

/* ================================================================= *
 *  Report Submission
 * ================================================================= */

static void on_report_to_send(const struct hid_report_to_send_event *evt)
{
	const struct device *dev;
	bool *in_flight;
	bool *iface_ready;
	uint8_t *tx_buf;

	/* Route by report size: 2 bytes → consumer, 8/29 bytes → keyboard */
	if (evt->report_size == CONSUMER_REPORT) {
		dev = _consumer_dev;
		in_flight = &_consumer_in_flight;
		iface_ready = &_consumer_iface_ready;
		tx_buf = _consumer_tx;
	} else {
		dev = _kbd_dev;
		in_flight = &_kbd_in_flight;
		iface_ready = &_kbd_iface_ready;
		tx_buf = _kbd_tx;
	}

	if (!*iface_ready) {
		return;
	}

	if (*in_flight) {
		LOG_WRN("Drop report (size=%u) while previous in flight",
			evt->report_size);
		return;
	}

	memcpy(tx_buf, evt->report, evt->report_size);
	int err = hid_device_submit_report(dev, evt->report_size, tx_buf);

	if (err) {
		LOG_ERR("Report submit failed: %d", err);
		hid_report_sent_event_submit();
	} else {
		*in_flight = true;
	}
}

/* ================================================================= *
 *  Init
 * ================================================================= */

static int init(void)
{
	if (register_hid_devices() != 0) {
		return -ENODEV;
	}

	if (usbd_setup() != 0) {
		return -ENODEV;
	}

	module_set_state(MODULE_STATE_READY);
	LOG_INF("USB HID transport initialised");
	return 0;
}

/* ================================================================= *
 *  CAF Event Listener
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
		if (_powered_down) { return false; }
		_powered_down = true;
		_kbd_in_flight = false;
		_consumer_in_flight = false;
		module_set_state(MODULE_STATE_OFF);
		return false;
	}

	if (is_wake_up_event(aeh)) {
		_powered_down = false;
		module_set_state(MODULE_STATE_READY);
		return false;
	}

	if (is_hid_report_to_send_event(aeh)) {
		on_report_to_send(cast_hid_report_to_send_event(aeh));
		return false;
	}

	if (is_mode_event(aeh)) {
		const struct mode_event *evt = cast_mode_event(aeh);

		if (evt->mode == KEYBOARD_MODE_USB) {
			usbd_enable(&usbd_ctx);
		} else {
			usbd_disable(&usbd_ctx);
			_kbd_in_flight = false;
			_consumer_in_flight = false;
		}
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);
APP_EVENT_SUBSCRIBE(MODULE, hid_report_to_send_event);
APP_EVENT_SUBSCRIBE(MODULE, mode_event);
