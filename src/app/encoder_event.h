/*
 * 蓝牙小键盘 - 编码器旋转事件（Route B）
 */

#ifndef _ENCODER_EVENT_H_
#define _ENCODER_EVENT_H_

#include <stdbool.h>
#include <stdint.h>

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 旋钮旋转事件：每转过 N 卡点由 encoder.c 桥接投递一次 */
struct encoder_event {
	struct app_event_header header;

	/** 本次旋转卡点数（>= 1） */
	uint8_t detents;

	/** 方向：true=顺时针，false=逆时针（以硬件标定为准） */
	bool cw;
};

APP_EVENT_TYPE_DECLARE(encoder_event);

#ifdef __cplusplus
}
#endif

#endif /* _ENCODER_EVENT_H_ */
