/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _ENCODER_EVENT_H_
#define _ENCODER_EVENT_H_

#include <app_event_manager.h>
#include <app_event_manager_profiler_tracer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 编码器旋转事件
 *
 * steps > 0: CW (顺时针)
 * steps < 0: CCW (逆时针)
 * 绝对值表示卡点步数
 */
struct encoder_event {
	struct app_event_header header;
	int8_t steps;
};

APP_EVENT_TYPE_DECLARE(encoder_event);

/** @brief 提交编码器事件 */
static inline void encoder_event_submit(int8_t steps)
{
	if (steps == 0) {
		return;
	}
	struct encoder_event *event = new_encoder_event();

	event->steps = steps;
	APP_EVENT_SUBMIT(event);
}

#ifdef __cplusplus
}
#endif

#endif /* _ENCODER_EVENT_H_ */
