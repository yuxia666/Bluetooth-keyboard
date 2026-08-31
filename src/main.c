/*
 * 蓝牙小键盘 - 主程序（CAF 标准，对齐参考 keyboard 工程）
 *
 * 仅初始化 app_event_manager 并上报 main READY；
 * CAF buttons 矩阵扫描/唤醒、QDEC 编码器、power_mgr 电池、
 * keyboard_core 按键消费均由各模块在 main READY 后自动初始化。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>

#define MODULE main
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(MODULE);

int main(void)
{
	if (app_event_manager_init()) {
		LOG_ERR("Application Event Manager not initialized");
		return 0;
	}

	module_set_state(MODULE_STATE_READY);

	return 0;
}
