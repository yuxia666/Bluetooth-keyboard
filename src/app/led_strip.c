/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * WS2812 灯带模块（app 层）
 *
 * 合并 vFINAL 实测方案 + 项目事件架构：
 *   - SPI3: MOSI=P0.20, SCK=P0.17（外部 NC 但必须配置）
 *   - SPI 帧: 6.4MHz + 0x70/0x40（实测值）
 *   - 供电: gpio-leds + led_on/led_off（P0.13 高有效）
 *   - 灯效: ~1s 渐隐（每 16ms 降 4）
 *   - 事件: button_event / theme_rgb_update_event / led_strip_en_event
 *           / power_down / wake_up
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

#include <app_event_manager.h>

#define MODULE led_strip
#include <caf/events/module_state_event.h>
#include <caf/events/button_event.h>
#include <caf/events/power_event.h>
#include <caf/key_id.h>

#include "events/theme_rgb_update_event.h"
#include "events/led_strip_en_event.h"
#include "led_strip.h"

LOG_MODULE_REGISTER(MODULE);

/* ===== 硬件常量 ===== */
#define LED_CHAIN_LENGTH	17
#define LED_STRIP_NODE		DT_ALIAS(led_strip)
#define RGB_PWR_NODE		DT_NODELABEL(rgb_pwr_en)

/* ===== Key Fade 参数（vFINAL: ~1s） ===== */
#define FADE_PERIOD_MS		16
#define FADE_STEP		4	/* 255/4 → ~1024ms */

/* ===== 默认主题色（绿色） ===== */
#define THEME_DEFAULT_R		0
#define THEME_DEFAULT_G		255
#define THEME_DEFAULT_B		0

/* ===== 模块状态 ===== */
static const struct device *_led_dev;
static const struct device *_pwr_dev;
static bool _pwr_on;

static struct led_rgb _theme_color = {
	.r = THEME_DEFAULT_R,
	.g = THEME_DEFAULT_G,
	.b = THEME_DEFAULT_B,
};
static bool _strip_enabled = true;
static bool _powered_down;

static struct led_rgb _pixels[LED_CHAIN_LENGTH];
static uint8_t _brightness[LED_CHAIN_LENGTH];
static bool _active[LED_CHAIN_LENGTH];
static struct k_timer _fade_timer;
static struct k_work _refresh_work;

/* ===== 键位(row,col) -> LED 索引映射（vFINAL 表，布局与参考工程一致） ===== */
static const uint8_t _led_map[6][4] = {
	{ 0xFF, 0xFF, 0xFF, 0xFF },	/* ROW0: 编码器按键无灯 */
	{ 0,    1,    2,    3    },	/* ROW1: NumLock / * -  */
	{ 4,    5,    6,    0xFF },	/* ROW2: 7 8 9          */
	{ 7,    8,    9,    10   },	/* ROW3: 4 5 6 +        */
	{ 11,   12,   13,   0xFF },	/* ROW4: 1 2 3          */
	{ 14,   15,   0xFF, 16   },	/* ROW5: 0 . Enter      */
};

/* ===== 供电控制 ===== */

static void set_pwr(bool on)
{
	if (_pwr_dev == NULL) {
		return;
	}
	if (on) {
		led_on(_pwr_dev, 0);
	} else {
		led_off(_pwr_dev, 0);
	}
	_pwr_on = on;
}

/* ===== Strip 刷新 ===== */

static void refresh_strip(void)
{
	if (_led_dev == NULL) {
		return;
	}
	for (int i = 0; i < LED_CHAIN_LENGTH; i++) {
		if (!_active[i] || _brightness[i] == 0) {
			_pixels[i].r = 0;
			_pixels[i].g = 0;
			_pixels[i].b = 0;
		} else {
			_pixels[i].r = (uint8_t)((_theme_color.r * _brightness[i]) / 255);
			_pixels[i].g = (uint8_t)((_theme_color.g * _brightness[i]) / 255);
			_pixels[i].b = (uint8_t)((_theme_color.b * _brightness[i]) / 255);
		}
	}
	led_strip_update_rgb(_led_dev, _pixels, LED_CHAIN_LENGTH);
}

static void clear_strip(void)
{
	memset(_pixels, 0, sizeof(_pixels));
	memset(_brightness, 0, sizeof(_brightness));
	memset(_active, 0, sizeof(_active));
	k_work_submit(&_refresh_work);
}

/* ===== Fade 定时器 ===== */

/* 工作队列刷新：避免在 k_timer 中断回调中调用阻塞式 SPI */
static void refresh_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	refresh_strip();
}

static void fade_tick_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	bool any_active = false;

	for (int i = 0; i < LED_CHAIN_LENGTH; i++) {
		if (_active[i]) {
			if (_brightness[i] > FADE_STEP) {
				_brightness[i] -= FADE_STEP;
				any_active = true;
			} else {
				_brightness[i] = 0;
				_active[i] = false;
			}
		}
	}
	k_work_submit(&_refresh_work);

	if (!any_active) {
		k_timer_stop(&_fade_timer);
	}
}

/* ===== 事件处理 ===== */

static void handle_button(const struct button_event *btn)
{
	/* 只处理按下事件，释放不触发 fade */
	if (!btn->pressed) {
		return;
	}
	if (!_strip_enabled || _powered_down) {
		return;
	}

	uint8_t col = KEY_COL(btn->key_id);
	uint8_t row = KEY_ROW(btn->key_id);

	if (col < 4 && row < 6) {
		uint8_t led_idx = _led_map[row][col];

		if (led_idx != 0xFF && led_idx < LED_CHAIN_LENGTH) {
			_brightness[led_idx] = 255;
			_active[led_idx] = true;
			k_timer_start(&_fade_timer, K_MSEC(FADE_PERIOD_MS),
				      K_MSEC(FADE_PERIOD_MS));
			k_work_submit(&_refresh_work);
		}
	}
}

static void handle_theme_update(const struct theme_rgb_update_event *evt)
{
	_theme_color = evt->theme_color;
	k_work_submit(&_refresh_work);
}

static void handle_strip_enable(const struct led_strip_en_event *evt)
{
	_strip_enabled = evt->enabled;
	if (_strip_enabled) {
		set_pwr(true);
		clear_strip();
	} else {
		k_timer_stop(&_fade_timer);
		clear_strip();
		set_pwr(false);
	}
}

/* ===== CAF 生命周期 ===== */

static bool app_event_handler(const struct app_event_header *aeh)
{
	if (is_module_state_event(aeh)) {
		const struct module_state_event *evt = cast_module_state_event(aeh);

		if (evt->state == MODULE_STATE_READY &&
		    evt->module_id == MODULE_ID(main)) {
			led_strip_init();
		}
		return false;
	}

	if (is_button_event(aeh)) {
		handle_button(cast_button_event(aeh));
		return false;
	}

	if (is_theme_rgb_update_event(aeh)) {
		handle_theme_update(cast_theme_rgb_update_event(aeh));
		return false;
	}

	if (is_led_strip_en_event(aeh)) {
		handle_strip_enable(cast_led_strip_en_event(aeh));
		return false;
	}

	if (is_power_down_event(aeh)) {
		if (!_powered_down) {
			_powered_down = true;
			k_timer_stop(&_fade_timer);
			clear_strip();
			set_pwr(false);
			module_set_state(MODULE_STATE_OFF);
		}
		return false;
	}

	if (is_wake_up_event(aeh)) {
		if (_powered_down) {
			_powered_down = false;
			if (_strip_enabled) {
				set_pwr(true);
				clear_strip();
			}
			module_set_state(MODULE_STATE_READY);
		}
		return false;
	}

	return false;
}

APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, button_event);
APP_EVENT_SUBSCRIBE(MODULE, theme_rgb_update_event);
APP_EVENT_SUBSCRIBE(MODULE, led_strip_en_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);

/* ===== 初始化 ===== */

int led_strip_init(void)
{
	k_timer_init(&_fade_timer, fade_tick_handler, NULL);
	k_work_init(&_refresh_work, refresh_work_handler);

	_led_dev = DEVICE_DT_GET(LED_STRIP_NODE);
	if (!device_is_ready(_led_dev)) {
		LOG_ERR("LED strip device not ready");
		return -ENODEV;
	}

	/* 供电 gpio-leds */
	_pwr_dev = DEVICE_DT_GET(RGB_PWR_NODE);
	if (!device_is_ready(_pwr_dev)) {
		LOG_ERR("RGB power device not ready");
		return -ENODEV;
	}

	/* 初始化即常开（vFINAL: led_on(pwr_dev, 0)） */
	if (_strip_enabled) {
		set_pwr(true);
	}
	clear_strip();

	LOG_INF("LED strip initialised (%u LEDs)", (unsigned)LED_CHAIN_LENGTH);
	return 0;
}
