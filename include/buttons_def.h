/*
 * 蓝牙小键盘 - CAF buttons 行列定义
 *
 * 列 = 输出高有效（扫描源），行 = 输入 + 下拉 + 高有效（默认极性）。
 * 与参考 keyboard 工程 inc/buttons_def.h 一致，也等价于原 matrix.c 的扫描方式。
 */

#include <caf/gpio_pins.h>

static const struct gpio_pin col[] = {
	{ .port = 0, .pin = 5 },   /* COL0 P0.05 */
	{ .port = 0, .pin = 6 },   /* COL1 P0.06 */
	{ .port = 0, .pin = 26 },  /* COL2 P0.26 */
	{ .port = 0, .pin = 30 },  /* COL3 P0.30 */
};

static const struct gpio_pin row[] = {
	{ .port = 0, .pin = 15 },  /* ROW0 P0.15 */
	{ .port = 0, .pin = 7 },   /* ROW1 P0.07 */
	{ .port = 0, .pin = 12 },  /* ROW2 P0.12 */
	{ .port = 0, .pin = 4 },   /* ROW3 P0.04 */
	{ .port = 1, .pin = 9 },   /* ROW4 P1.09 */
	{ .port = 0, .pin = 8 },   /* ROW5 P0.08 */
};
