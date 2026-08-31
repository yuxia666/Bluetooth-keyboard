/*
 * 蓝牙小键盘 - 按键/编码器输出模块（Route B）
 *
 * 订阅：
 *   button_event  -> 矩阵按键 RTT 打印（保持原“按下打印”行为）
 *   encoder_event -> 编码器旋转 RTT 打印（方向 + 卡点数 + 角度）
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <app_event_manager.h>
#include <caf/events/button_event.h>
#include <caf/key_id.h>

#include "encoder_event.h"

#define ROW_COUNT 6
#define COL_COUNT 4

#define DEGREES_PER_DETENT 18

/* 6 行 x 4 列键名表（行=ROW0..ROW5，列=COL0..COL3） */
static const char * const key_names[ROW_COUNT][COL_COUNT] = {
	/* COL0       COL1    COL2    COL3 */
	{ NULL,       NULL,   NULL,   "旋钮按键" },     /* ROW0 */
	{ "数字锁定", "除号", "乘号", "减号" },         /* ROW1 */
	{ "7",        "8",    "9",    NULL },           /* ROW2 */
	{ "4",        "5",    "6",    "加号" },         /* ROW3 */
	{ "1",        "2",    "3",    NULL },           /* ROW4 */
	{ "0",        "小数点", NULL,  "回车" },         /* ROW5 */
};

static int32_t knob_angle_deg;

static bool handle_button_event(const struct button_event *evt)
{
	uint8_t col = KEY_COL(evt->key_id);
	uint8_t row = KEY_ROW(evt->key_id);
	const char *name = key_names[row][col];

	/* 保持原有行为：只在按下时打印 */
	if (!evt->pressed) {
		return false;
	}

	if (name != NULL) {
		printk("[%u,%u]%s\n", row, col, name);
	} else {
		printk("[%u,%u]未定义键\n", row, col);
	}

	return false;
}

static bool handle_encoder_event(const struct encoder_event *evt)
{
	int32_t delta = (int32_t)evt->detents * DEGREES_PER_DETENT *
			(evt->cw ? 1 : -1);

	knob_angle_deg += delta;

	printk("旋钮旋转 %s %u 卡点 (本次 %s%d°, 累计 %s%d°)\n",
	       evt->cw ? "顺时针" : "逆时针",
	       evt->detents,
	       delta >= 0 ? "+" : "", delta,
	       knob_angle_deg >= 0 ? "+" : "", knob_angle_deg);

	return false;
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_button_event(aeh)) {
		return handle_button_event(cast_button_event(aeh));
	}

	if (is_encoder_event(aeh)) {
		return handle_encoder_event(cast_encoder_event(aeh));
	}

	return false;
}

APP_EVENT_LISTENER(keymap, app_event_handler);
APP_EVENT_SUBSCRIBE(keymap, button_event);
APP_EVENT_SUBSCRIBE(keymap, encoder_event);
