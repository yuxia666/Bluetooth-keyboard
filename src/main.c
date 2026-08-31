/*
 * 蓝牙小键盘 - 主程序（Route B）
 *
 * 初始化 CAF 事件管理器后，启动矩阵扫描循环。
 * 编码器事件由 gpio-qdec -> encoder.c 自动桥接。
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <app_event_manager.h>
#include <caf/events/module_state_event.h>

#include "app/matrix.h"

int main(void)
{
	int ret;

	if (app_event_manager_init()) {
		printk("应用事件管理器初始化失败\n");
		return 0;
	}

	ret = matrix_init();
	if (ret < 0) {
		printk("矩阵按键 GPIO 初始化失败: %d\n", ret);
		return 0;
	}

	printk("矩阵按键扫描已启动\n");

	while (1) {
		matrix_scan();
		k_msleep(5);
	}

	return 0;
}
