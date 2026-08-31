/*
 * 蓝牙小键盘 - 编码器桥接模块（gpio-qdec）
 *
 * 订阅 gpio-qdec 的 INPUT_REL_WHEEL 事件，
 * 转换为自定义 CAF encoder_event 后提交给事件管理器。
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <app_event_manager.h>
#include "encoder_event.h"

#define ENCODER_DEV_NODE DT_NODELABEL(encoder)

static void encoder_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->code != INPUT_REL_WHEEL || !evt->sync || evt->value == 0) {
		return;
	}

	struct encoder_event *event = new_encoder_event();

	event->detents = (uint8_t)(evt->value > 0 ? evt->value : -evt->value);
	event->cw = (evt->value > 0);

	APP_EVENT_SUBMIT(event);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(ENCODER_DEV_NODE), encoder_input_cb, NULL);
