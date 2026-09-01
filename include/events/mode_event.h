/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MODE_EVENT_H_
#define _MODE_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 键盘工作模式 */
enum keyboard_mode {
	KEYBOARD_MODE_USB,   /* 0: USB HID */
	KEYBOARD_MODE_2_4G,  /* 1: 2.4G（预留，本期不实现） */
	KEYBOARD_MODE_BLE,   /* 2: BLE HID */
};

struct mode_event {
	struct app_event_header header;
	enum keyboard_mode mode;
};

APP_EVENT_TYPE_DECLARE(mode_event);

#ifdef __cplusplus
}
#endif

#endif /* _MODE_EVENT_H_ */
