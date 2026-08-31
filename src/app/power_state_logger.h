/*
 * 蓝牙小键盘 - 电源状态日志模块（Route B + CAF Power Manager）
 */

#ifndef _POWER_STATE_LOGGER_H_
#define _POWER_STATE_LOGGER_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 当前是否处于低功耗 suspended 状态。
 */
bool power_state_is_suspended(void);

#ifdef __cplusplus
}
#endif

#endif /* _POWER_STATE_LOGGER_H_ */
