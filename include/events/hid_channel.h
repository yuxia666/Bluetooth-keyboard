/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * HID 发送通道枚举
 */

#ifndef _HID_CHANNEL_H_
#define _HID_CHANNEL_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief HID 发送通道 */
enum hid_channel {
	/** USB 键盘通道（NKRO/Boot 报告） */
	HID_CHANNEL_USB_KBD = 0,
	/** USB Consumer 通道（2B pulse） */
	HID_CHANNEL_USB_CONSUMER,
	/** BLE 共享通道（键盘 + Consumer 复用） */
	HID_CHANNEL_BLE_SHARED,

	HID_CHANNEL_COUNT
};

#ifdef __cplusplus
}
#endif

#endif /* _HID_CHANNEL_H_ */
