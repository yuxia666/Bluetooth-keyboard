/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Shared HID Report Maps — used by USB and BLE transports.
 */

#ifndef _HID_REPORT_MAPS_H_
#define _HID_REPORT_MAPS_H_

#include <stdint.h>

/* ── Report Sizes ── */
#define HID_KBD_BOOT_SIZE   8
#define HID_KBD_NKRO_SIZE  29
#define HID_CONSUMER_SIZE   2

/* ── Report IDs (BLE only) ── */
#define BLE_REPORT_ID_KEYBOARD  1
#define BLE_REPORT_ID_CONSUMER  2

/* ── Input Report Indices (BLE bt_hids) ── */
#define BLE_INPUT_REP_KBD_IDX  0
#define BLE_INPUT_REP_CC_IDX   1

/* ══════════════════════════════════════════════════════════════════
 * USB Report Descriptors (no Report ID, dual-interface)
 * ══════════════════════════════════════════════════════════════════ */

/*
 * Keyboard: 29-byte Variable bitmap NKRO (no report ID).
 * Byte 0 = modifier, bytes 1-28 = bitmap (224 bits for usage 0x00-0xDF).
 * kbdhid.sys binds to this because there is no report ID to misparse.
 */
static const uint8_t usb_kbd_report_desc[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, /* GD, Keyboard, Collection */
	0x05, 0x07,                         /* Keyboard Page */
	0x19, 0xE0, 0x29, 0xE7,             /* Modifier: 8x1bit Var (0xE0-0xE7) */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x05, 0x07,                         /* Keyboard Page */
	0x19, 0x00, 0x2A, 0xDF, 0x00,      /* Bitmap: 224x1bit Var (0x00-0xDF) */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x96, 0xE0, 0x00, 0x81, 0x02,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, /* LED Output: 5bit + 3pad */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
	0x75, 0x03, 0x95, 0x01, 0x91, 0x01,
	0xC0
};

/*
 * Consumer Control: 2-byte Array (no report ID).
 */
static const uint8_t usb_consumer_report_desc[] = {
	0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, /* Consumer, CC, Collection */
	0x15, 0x00, 0x26, 0xFF, 0x03,      /* 0..1023 */
	0x19, 0x00, 0x2A, 0xFF, 0x03,
	0x75, 0x10, 0x95, 0x01,             /* 16-bit Array */
	0x81, 0x00,                         /* Input (Data,Array,Abs) */
	0xC0
};

/* ── BLE Report Sizes (including Report ID byte) ── */
#define BLE_KBD_REPORT_SIZE  30  /* 1B ID + 29B NKRO */
#define BLE_CC_REPORT_SIZE    3  /* 1B ID + 2B data */

/* ══════════════════════════════════════════════════════════════════
 * BLE Report Map (single HID Service, Report IDs)
 * ══════════════════════════════════════════════════════════════════ */

/*
 * Combined Keyboard + Consumer Control.
 * Report ID 1: Keyboard (29-byte NKRO bitmap — same format as USB)
 * Report ID 2: Consumer Control (2-byte: 16-bit usage)
 */
static const uint8_t ble_report_map[] = {
	/* ── Report ID 1: Keyboard (NKRO bitmap) ── */
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, /* GD, Keyboard, Collection */
	0x85, 0x01,                         /*   Report ID 1 */
	0x05, 0x07,                         /*   Keyboard Page */
	0x19, 0xE0, 0x29, 0xE7,             /*   Modifier: 8x1bit Var (0xE0-0xE7) */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x05, 0x07,                         /*   Keyboard Page */
	0x19, 0x00, 0x2A, 0xDF, 0x00,      /*   Bitmap: 224x1bit Var (0x00-0xDF) */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x96, 0xE0, 0x00, 0x81, 0x02,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, /*   LED Output: 5bit + 3pad */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02,
	0x75, 0x03, 0x95, 0x01, 0x91, 0x01,
	0xC0,                             /* End Collection */

	/* ── Report ID 2: Consumer Control ── */
	0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, /* Consumer, CC, Collection */
	0x85, 0x02,                         /*   Report ID 2 */
	0x15, 0x00, 0x26, 0xFF, 0x03,      /*   0..1023 */
	0x19, 0x00, 0x2A, 0xFF, 0x03,
	0x75, 0x10, 0x95, 0x01,             /*   16-bit Array */
	0x81, 0x00,                         /*   Input (Data,Array,Abs) */
	0xC0                              /* End Collection */
};

#endif /* _HID_REPORT_MAPS_H_ */
