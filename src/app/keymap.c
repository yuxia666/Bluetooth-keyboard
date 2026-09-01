/*
 * 蓝牙小键盘 - 按键/HID 输出日志模块
 *
 * 对齐参考 keyboard 工程：按键消费由 keyboard_core.c 负责（构建 HID 报告），
 * 本模块作为消费者在 RTT 打印：
 *   - button_event      -> 矩阵按键中文名（保留原日志行为）
 *   - hid_key_event     -> HID 键盘报告（boot/NKRO 字节）
 *   - hid_consumer_event -> HID 消费控制报告（音量等 usage）
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>
#include <caf/events/button_event.h>
#include <caf/key_id.h>

#include "events/hid_key_event.h"
#include "events/hid_consumer_event.h"
#include "events/hid_led_event.h"

LOG_MODULE_REGISTER(keymap, CONFIG_LOG_DEFAULT_LEVEL);

#define ROW_COUNT 6
#define COL_COUNT 4

/* 6 行 x 4 列键名表（行=ROW0..ROW5，列=COL0..COL3） */
static const char * const key_names[ROW_COUNT][COL_COUNT] = {
	/* COL0       COL1    COL2    COL3 */
	{ NULL,       NULL,   NULL,   "旋钮按键" },     /* ROW0 */
	{ "数字锁定", "乘号", "除号", "减号" },         /* ROW1: NumLk, *, /, - */
	{ "7",        "8",    "9",    NULL },           /* ROW2 */
	{ "4",        "5",    "6",    "加号" },         /* ROW3 */
	{ "1",        "2",    "3",    NULL },           /* ROW4 */
	{ "0",        "小数点", NULL,  "回车" },         /* ROW5 */
};

static bool handle_button_event(const struct button_event *evt)
{
	uint8_t col = KEY_COL(evt->key_id);
	uint8_t row = KEY_ROW(evt->key_id);
	const char *name = (row < ROW_COUNT && col < COL_COUNT)
			   ? key_names[row][col] : NULL;

	LOG_INF("[%u,%u]%s%s", row, col,
		name != NULL ? name : "未定义键",
		evt->pressed ? " 按下" : " 释放");

	return false;
}

static bool handle_hid_key_event(const struct hid_key_event *evt)
{
	/* HID 键盘报告由 USB transport 发送，此处不再打印（避免刷屏） */
	ARG_UNUSED(evt);

	return false;
}

static bool handle_hid_consumer_event(const struct hid_consumer_event *evt)
{
	LOG_INF("HID consumer usage=0x%04x", evt->usage);

	return false;
}

static bool handle_hid_led_event(const struct hid_led_event *evt)
{
	LOG_INF("HID LED state=0x%02x", evt->led_state);

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_button_event(aeh)) {
		return handle_button_event(cast_button_event(aeh));
	}

	if (is_hid_key_event(aeh)) {
		return handle_hid_key_event(cast_hid_key_event(aeh));
	}

	if (is_hid_consumer_event(aeh)) {
		return handle_hid_consumer_event(cast_hid_consumer_event(aeh));
	}

	if (is_hid_led_event(aeh)) {
		return handle_hid_led_event(cast_hid_led_event(aeh));
	}

	return false;
}

APP_EVENT_LISTENER(keymap, app_event_handler);
APP_EVENT_SUBSCRIBE(keymap, button_event);
APP_EVENT_SUBSCRIBE(keymap, hid_key_event);
APP_EVENT_SUBSCRIBE(keymap, hid_consumer_event);
APP_EVENT_SUBSCRIBE(keymap, hid_led_event);
