/*
 * 蓝牙小键盘 - 矩阵按键扫描 + 真实低功耗唤醒
 *
 * 正常模式：
 *   行 = 输入 + 内部下拉 + 高有效
 *   列 = 输出 + 高有效，逐列扫描。
 *
 * 低功耗唤醒模式：
 *   列 = 全部输出高，行 = 输入 + 内部下拉 + 上升沿中断。
 *   任意按键按下会把对应行拉高，触发行 GPIO 中断，
 *   中断中唤醒 main 线程，main 恢复矩阵扫描后提交 wake_up_event。
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <app_event_manager.h>
#include <caf/events/button_event.h>
#include <caf/events/power_event.h>
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

struct row_wake_data {
	struct gpio_callback cb;
	uint8_t row;
};

static struct row_wake_data row_wake[ROW_COUNT];
static struct k_sem matrix_wake_sem;
static bool suspended;

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

	k_sem_init(&matrix_wake_sem, 0, 1);
	suspended = false;

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

static void row_wake_isr(const struct device *port,
			 struct gpio_callback *cb,
			 gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	ARG_UNUSED(cb);

	/* 只唤醒 main 线程，具体按键由恢复扫描后重新识别 */
	k_sem_give(&matrix_wake_sem);
}

static void matrix_enter_suspend(void)
{
	int ret;

	if (suspended) {
		return;
	}

	/* 列全部输出高，作为“扫描源”（保持与正常扫描一致的二极管方向） */
	for (int i = 0; i < COL_COUNT; i++) {
		gpio_pin_configure_dt(&cols[i], GPIO_OUTPUT_ACTIVE);
	}

	/* 行改为输入 + 下拉 + 上升沿中断 */
	for (int i = 0; i < ROW_COUNT; i++) {
		row_wake[i].row = (uint8_t)i;
		gpio_init_callback(&row_wake[i].cb, row_wake_isr, BIT(rows[i].pin));
		gpio_add_callback(rows[i].port, &row_wake[i].cb);

		ret = gpio_pin_configure_dt(&rows[i], GPIO_INPUT | GPIO_PULL_DOWN);
		if (ret < 0) {
			continue;
		}

		gpio_pin_interrupt_configure_dt(&rows[i], GPIO_INT_EDGE_RISING);
	}

	suspended = true;
}

static void matrix_exit_suspend(void)
{
	if (!suspended) {
		return;
	}

	/* 关闭行中断 */
	for (int i = 0; i < ROW_COUNT; i++) {
		gpio_pin_interrupt_configure_dt(&rows[i], GPIO_INT_DISABLE);
		gpio_remove_callback(rows[i].port, &row_wake[i].cb);
	}

	/* 列恢复为输出低 */
	for (int i = 0; i < COL_COUNT; i++) {
		gpio_pin_configure_dt(&cols[i], GPIO_OUTPUT_INACTIVE);
	}

	/* 行恢复为输入 + 下拉 */
	for (int i = 0; i < ROW_COUNT; i++) {
		gpio_pin_configure_dt(&rows[i], GPIO_INPUT);
	}

	suspended = false;
}

bool matrix_is_suspended(void)
{
	return suspended;
}

void matrix_wait_wake(void)
{
	k_sem_take(&matrix_wake_sem, K_FOREVER);
}

void matrix_resume(void)
{
	if (suspended) {
		matrix_exit_suspend();
	}

	APP_EVENT_SUBMIT(new_wake_up_event());
}

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_power_down_event(aeh)) {
		matrix_enter_suspend();
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(matrix, app_event_handler);
APP_EVENT_SUBSCRIBE(matrix, power_down_event);
