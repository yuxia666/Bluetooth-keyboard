/*
 * 蓝牙小键盘 - 矩阵按键扫描（Route B）
 *
 * 将原有 main.c 中的手动 GPIO 矩阵扫描迁移为独立模块，
 * 扫描到按键状态变化后提交 CAF button_event。
 *
 * 矩阵：6 行 x 4 列
 * 极性：行 = 输入 + 内部下拉 + 高有效（按下为高）
 *       列 = 输出 + 高有效（扫描时逐列输出高）
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <app_event_manager.h>
#include <caf/events/button_event.h>
#include <caf/key_id.h>

#include "matrix.h"

#define ROW_COUNT 6
#define COL_COUNT 4

#define SCAN_INTERVAL_MS 5
#define DEBOUNCE_SAMPLES 2   /* 约 10ms 去抖 */

#define COL_SETTLE_US 50

static const struct gpio_dt_spec rows[ROW_COUNT] = {
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 15, .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW0 P0.15 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 7,  .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW1 P0.07 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 12, .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW2 P0.12 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 4,  .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW3 P0.04 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio1)), .pin = 9,  .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW4 P1.09 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 8,  .dt_flags = GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH }, /* ROW5 P0.08 */
};

static const struct gpio_dt_spec cols[COL_COUNT] = {
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 5,  .dt_flags = GPIO_ACTIVE_HIGH }, /* COL0 P0.05 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 6,  .dt_flags = GPIO_ACTIVE_HIGH }, /* COL1 P0.06 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 26, .dt_flags = GPIO_ACTIVE_HIGH }, /* COL2 P0.26 */
	{ .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)), .pin = 30, .dt_flags = GPIO_ACTIVE_HIGH }, /* COL3 P0.30 */
};

static bool gpios_ready(void)
{
	for (int i = 0; i < ROW_COUNT; i++) {
		if (!device_is_ready(rows[i].port)) {
			return false;
		}
	}

	for (int i = 0; i < COL_COUNT; i++) {
		if (!device_is_ready(cols[i].port)) {
			return false;
		}
	}

	return true;
}

int matrix_init(void)
{
	int ret;

	if (!gpios_ready()) {
		return -ENODEV;
	}

	for (int i = 0; i < COL_COUNT; i++) {
		ret = gpio_pin_configure_dt(&cols[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	for (int i = 0; i < ROW_COUNT; i++) {
		ret = gpio_pin_configure_dt(&rows[i], GPIO_INPUT);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static void submit_button_event(uint8_t row, uint8_t col, bool pressed)
{
	struct button_event *event = new_button_event();

	event->key_id = KEY_ID(col, row);
	event->pressed = pressed;

	APP_EVENT_SUBMIT(event);
}

void matrix_scan(void)
{
	/* 按下状态与去抖计数 */
	static bool pressed[ROW_COUNT][COL_COUNT];
	static uint8_t stable_count[ROW_COUNT][COL_COUNT];

	for (int c = 0; c < COL_COUNT; c++) {
		gpio_pin_set_dt(&cols[c], 1);
		k_busy_wait(COL_SETTLE_US);

		for (int r = 0; r < ROW_COUNT; r++) {
			bool raw = gpio_pin_get_dt(&rows[r]) > 0;

			if (raw) {
				if (stable_count[r][c] < DEBOUNCE_SAMPLES) {
					stable_count[r][c]++;
				}

				if (!pressed[r][c] && stable_count[r][c] >= DEBOUNCE_SAMPLES) {
					pressed[r][c] = true;
					submit_button_event((uint8_t)r, (uint8_t)c, true);
				}
			} else {
				if (stable_count[r][c] > 0) {
					stable_count[r][c]--;
				}

				if (pressed[r][c] && stable_count[r][c] == 0) {
					pressed[r][c] = false;
					submit_button_event((uint8_t)r, (uint8_t)c, false);
				}
			}
		}

		gpio_pin_set_dt(&cols[c], 0);
	}
}
