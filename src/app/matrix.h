/*
 * 蓝牙小键盘 - 矩阵按键模块（Route B）
 */

#ifndef _MATRIX_H_
#define _MATRIX_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化矩阵 GPIO。
 * @return 0 成功，负值失败。
 */
int matrix_init(void);

/**
 * @brief 扫描一次矩阵并提交 button_event。
 */
void matrix_scan(void);

#ifdef __cplusplus
}
#endif

#endif /* _MATRIX_H_ */
