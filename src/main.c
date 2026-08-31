/*
 * 蓝牙小键盘 - 主程序（Route B + CAF Power Manager）
 *
 * 初始化 CAF 事件管理器并上报 main READY，
 * 之后由 matrix_scan 持续扫描按键；电源管理由 CAF Power Manager 处理。
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <app_event_manager.h>

#define MODULE main
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

	/* 通知 CAF 各模块 main 已就绪，Power Manager 此时才会启动 */
	module_set_state(MODULE_STATE_READY);

	while (1) {
		matrix_scan();
		k_msleep(5);
	}

	return 0;
}
