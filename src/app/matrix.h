/*
 * 蓝牙小键盘 - 矩阵按键模块（Route B + 真实低功耗唤醒）
 */

#ifndef _MATRIX_H_
#define _MATRIX_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化矩阵 GPIO 和唤醒信号量。
 * @return 0 成功，负值失败。
 */
int matrix_init(void);

/**
 * @brief 扫描一次矩阵并提交 button_event。
 */
void matrix_scan(void);

/**
 * @brief 当前是否处于低功耗挂起状态。
 */
bool matrix_is_suspended(void);

/**
 * @brief 阻塞等待矩阵唤醒（低功耗时由列 GPIO 中断唤醒）。
 */
void matrix_wait_wake(void);

/**
 * @brief 退出低功耗矩阵模式并提交 wake_up_event。
 */
void matrix_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* _MATRIX_H_ */
