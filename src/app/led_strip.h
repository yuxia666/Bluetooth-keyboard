/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * WS2812 灯带模块（app 层）
 *
 * 订阅：
 *   - button_event：按键触发 Key Fade
 *   - theme_rgb_update_event：更新主题色
 *   - led_strip_en_event：启停灯带
 *   - power_down/wake_up：低功耗
 */

#ifndef _APP_LED_STRIP_H_
#define _APP_LED_STRIP_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 LED 灯带模块（由 main READY 触发） */
int led_strip_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _APP_LED_STRIP_H_ */
