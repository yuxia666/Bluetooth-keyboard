/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Injoinic IP5306 PMIC driver (out-of-tree).
 *
 * Features:
 *  - I2C read of charging/full status.
 *  - Keepalive pulse on wakeup GPIO using RTC2 interrupt + GPIO control,
 *    avoiding GPIOTE channel conflicts with matrix wake interrupts.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <nrfx.h>
#include <hal/nrf_rtc.h>

#include <drivers/ip5306.h>

#define DT_DRV_COMPAT injoinic_ip5306

/* IP5306 registers. */
#define IP5306_REG_STATUS0       0x70
#define IP5306_REG_STATUS0_CHG    BIT(3)
#define IP5306_REG_STATUS1       0x71
#define IP5306_REG_STATUS1_FULL   BIT(3)

/* nRF RTC2 is used for the hardware keepalive timer. */
#define IP5306_RTC               NRF_RTC2
#define IP5306_RTC_IRQ           RTC2_IRQn
#define IP5306_RTC_COUNTER_MASK  0x00FFFFFFUL

struct ip5306_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec wakeup_gpio;
	uint32_t keepalive_period_ms;
	uint32_t keepalive_pulse_ms;
};

struct ip5306_data {
	const struct device *dev;
};

static uint32_t keepalive_period_ticks;
static uint32_t keepalive_pulse_ticks;
static const struct gpio_dt_spec *keepalive_gpio;

static uint32_t ms_to_rtc_ticks(uint32_t ms)
{
	return ((uint64_t)ms * 32768UL) / 1000UL;
}

static void keepalive_rtc_isr(const void *arg)
{
	ARG_UNUSED(arg);

	if (nrf_rtc_event_check(IP5306_RTC, NRF_RTC_EVENT_COMPARE_0)) {
		nrf_rtc_event_clear(IP5306_RTC, NRF_RTC_EVENT_COMPARE_0);

		uint32_t now = nrf_rtc_counter_get(IP5306_RTC);

		/* 脉冲开始：P0.22 拉低（active low） */
		gpio_pin_set_dt(keepalive_gpio, 0);

		/* Pulse end: 500 ms after pulse start. */
		nrf_rtc_cc_set(IP5306_RTC, 1,
			       (now + keepalive_pulse_ticks) & IP5306_RTC_COUNTER_MASK);
		/* Next pulse start: 8000 ms after this one. */
		nrf_rtc_cc_set(IP5306_RTC, 0,
			       (now + keepalive_period_ticks) & IP5306_RTC_COUNTER_MASK);
	}

	if (nrf_rtc_event_check(IP5306_RTC, NRF_RTC_EVENT_COMPARE_1)) {
		nrf_rtc_event_clear(IP5306_RTC, NRF_RTC_EVENT_COMPARE_1);

		/* 脉冲结束：P0.22 拉高 */
		gpio_pin_set_dt(keepalive_gpio, 1);
	}
}

static int keepalive_start(const struct gpio_dt_spec *wakeup_gpio,
			   uint32_t period_ms,
			   uint32_t pulse_ms)
{
	int ret;

	if (!gpio_is_ready_dt(wakeup_gpio)) {
		return -ENODEV;
	}

	/* Inactive level is high; active keepalive pulse is low. */
	ret = gpio_pin_configure_dt(wakeup_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	keepalive_gpio = wakeup_gpio;
	keepalive_period_ticks = ms_to_rtc_ticks(period_ms);
	keepalive_pulse_ticks = ms_to_rtc_ticks(pulse_ms);

	/* RTC2 at 32.768 kHz，用于产生 keepalive 定时。 */
	nrf_rtc_prescaler_set(IP5306_RTC, 0);
	nrf_rtc_event_clear(IP5306_RTC, NRF_RTC_EVENT_COMPARE_0);
	nrf_rtc_event_clear(IP5306_RTC, NRF_RTC_EVENT_COMPARE_1);
	nrf_rtc_int_enable(IP5306_RTC,
			   NRF_RTC_INT_COMPARE0_MASK | NRF_RTC_INT_COMPARE1_MASK);

	uint32_t now = nrf_rtc_counter_get(IP5306_RTC);

	/* 启动时立即给一个 keepalive 脉冲，唤醒可能处于待机/关断状态的 IP5306 */
	gpio_pin_set_dt(wakeup_gpio, 0);
	nrf_rtc_cc_set(IP5306_RTC, 1,
		       (now + keepalive_pulse_ticks) & IP5306_RTC_COUNTER_MASK);
	nrf_rtc_cc_set(IP5306_RTC, 0,
		       (now + keepalive_period_ticks) & IP5306_RTC_COUNTER_MASK);

	IRQ_CONNECT(IP5306_RTC_IRQ, 0, keepalive_rtc_isr, NULL, 0);
	irq_enable(IP5306_RTC_IRQ);

	nrf_rtc_task_trigger(IP5306_RTC, NRF_RTC_TASK_START);

	return 0;
}

static int ip5306_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct ip5306_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int ip5306_device_init(const struct device *dev)
{
	const struct ip5306_config *cfg = dev->config;
	int ret;

	if (!device_is_ready(cfg->i2c.bus)) {
		return -ENODEV;
	}

	ret = keepalive_start(&cfg->wakeup_gpio,
			      cfg->keepalive_period_ms,
			      cfg->keepalive_pulse_ms);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void ip5306_wake_pulse(const struct gpio_dt_spec *wakeup_gpio)
{
	/* 读 I2C 前给一个短暂低脉冲，确保 IP5306 处于工作态 */
	gpio_pin_set_dt(wakeup_gpio, 0);
	k_busy_wait(1000);
	gpio_pin_set_dt(wakeup_gpio, 1);
}

static int ip5306_get_status_device(const struct device *dev,
				    struct ip5306_status *status)
{
	const struct ip5306_config *cfg = dev->config;
	uint8_t reg0;
	uint8_t reg1;
	int ret;

	if (status == NULL) {
		return -EINVAL;
	}

	for (int attempt = 0; attempt < 3; attempt++) {
		/* 读取前先唤醒 IP5306，避免其处于待机导致 NACK */
		ip5306_wake_pulse(&cfg->wakeup_gpio);

		ret = ip5306_read_reg(dev, IP5306_REG_STATUS0, &reg0);
		if (ret < 0) {
			k_msleep(10);
			continue;
		}

		ret = ip5306_read_reg(dev, IP5306_REG_STATUS1, &reg1);
		if (ret < 0) {
			k_msleep(10);
			continue;
		}

		status->charging = (reg0 & IP5306_REG_STATUS0_CHG) != 0;
		status->full = (reg1 & IP5306_REG_STATUS1_FULL) != 0;

		return 0;
	}

	return ret;
}

int ip5306_init(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(injoinic_ip5306));

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	return 0;
}

int ip5306_get_status(struct ip5306_status *status)
{
	const struct device *dev = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(injoinic_ip5306));

	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	return ip5306_get_status_device(dev, status);
}

#define IP5306_INIT(n)							\
	static int ip5306_init_##n(const struct device *dev)		\
	{								\
		return ip5306_device_init(dev);				\
	}								\
	static struct ip5306_data ip5306_data_##n;			\
	static const struct ip5306_config ip5306_config_##n = {	\
		.i2c = I2C_DT_SPEC_INST_GET(n),			\
		.wakeup_gpio = GPIO_DT_SPEC_INST_GET(n, wakeup_gpios),	\
		.keepalive_period_ms = DT_INST_PROP(n, keepalive_period_ms), \
		.keepalive_pulse_ms = DT_INST_PROP(n, keepalive_pulse_ms), \
	};								\
	DEVICE_DT_INST_DEFINE(n, ip5306_init_##n, NULL,			\
			      &ip5306_data_##n, &ip5306_config_##n,	\
			      POST_KERNEL, CONFIG_IP5306_INIT_PRIORITY,	\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(IP5306_INIT)
