/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Injoinic IP5306 PMIC driver — aligned with reference keyboard project.
 *
 * Features:
 *  - I2C read of charging/full status.
 *  - BOOST always-on to prevent IP5306 auto-shutdown.
 *  - Keepalive pulse on wakeup GPIO using RTC2 + PPI + GPIOTE hardware
 *    (no CPU involvement, no per-pulse interrupts).
 *  - suspend()/resume() to stop/start the keep-alive on power transitions.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <drivers/ip5306.h>

#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
#include <helpers/nrfx_gppi.h>
#include <nrfx_gpiote.h>
#include <hal/nrf_rtc.h>
#include <hal/nrf_gpiote.h>
#endif

#define DT_DRV_COMPAT injoinic_ip5306

LOG_MODULE_REGISTER(ip5306, CONFIG_LOG_DEFAULT_LEVEL);

/* IP5306 registers */
#define REG_SYS_CTL0     0x00
#define REG_SYS_CTL2     0x02
#define REG_READ0        0x70
#define REG_READ1        0x71

#define SYS_CTL0_BOOST_ALWAYS_ON  BIT(1)

#define REG_READ0_CHARGE_EN       BIT(3)
#define REG_READ1_FULL_FLAG       BIT(3)

/* ================================================================= *
 *  Data structures
 * ================================================================= */

struct ip5306_config {
	struct i2c_dt_spec  i2c;
	struct gpio_dt_spec wakeup_gpio;
	uint32_t            keep_alive_interval_ms;
	uint32_t            pulse_width_ms;
};

struct ip5306_data {
	const struct device *dev;
	bool                 suspended;
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)
	nrfx_gppi_handle_t   ppi_cycle;
	nrfx_gppi_handle_t   ppi_pulse_end;
	uint32_t             ppi_cycle_eep;
	uint32_t             ppi_cycle_tep;
	uint32_t             ppi_pulse_eep;
	uint32_t             ppi_pulse_tep;
	uint8_t              gpiote_ch_set;
	uint8_t              gpiote_ch_clr;
#else
	struct k_work_delayable keep_alive_work;
	struct k_work_delayable pulse_end_work;
#endif
};

/* ================================================================= *
 *  I2C helpers
 * ================================================================= */

static int reg_read(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct ip5306_config *cfg = dev->config;

	return i2c_reg_read_byte_dt(&cfg->i2c, reg, val);
}

static int reg_update(const struct device *dev, uint8_t reg,
		      uint8_t mask, uint8_t val)
{
	const struct ip5306_config *cfg = dev->config;

	return i2c_reg_update_byte_dt(&cfg->i2c, reg, mask, val);
}

/* ================================================================= *
 *  Public API
 * ================================================================= */

int ip5306_charge_status(const struct device *dev,
			  enum ip5306_charge_state *state)
{
	uint8_t r70, r71;
	int err;

	err = reg_read(dev, REG_READ0, &r70);
	if (err) {
		return err;
	}
	err = reg_read(dev, REG_READ1, &r71);
	if (err) {
		return err;
	}

	bool charging = (r70 & REG_READ0_CHARGE_EN) != 0;
	bool full     = (r71 & REG_READ1_FULL_FLAG) != 0;

	if (!charging) {
		*state = IP5306_CHARGE_DISABLED;
	} else if (full) {
		*state = IP5306_CHARGE_FULL;
	} else {
		*state = IP5306_CHARGE_ACTIVE;
	}

	return 0;
}

int ip5306_is_charging(const struct device *dev, bool *charging)
{
	uint8_t reg;
	int err = reg_read(dev, REG_READ0, &reg);

	if (err) {
		return err;
	}
	*charging = (reg & REG_READ0_CHARGE_EN) != 0;
	return 0;
}

int ip5306_is_full(const struct device *dev, bool *full)
{
	uint8_t reg;
	int err = reg_read(dev, REG_READ1, &reg);

	if (err) {
		return err;
	}
	*full = (reg & REG_READ1_FULL_FLAG) != 0;
	return 0;
}

int ip5306_wakeup(const struct device *dev)
{
	/* Triggered via RTC + PPI or k_work; this function is
	 * a no-op because the hardware/cpu path handles it.
	 */
	ARG_UNUSED(dev);
	return 0;
}

/* ================================================================= *
 *  Keep-alive paths
 * ================================================================= */

/* ------- PPI path (nRF hardware, no CPU) ------------------------- */
#if defined(CONFIG_SOC_FAMILY_NORDIC_NRF)

static nrfx_gpiote_t gpiote_inst = NRFX_GPIOTE_INSTANCE(NRF_GPIOTE);

static int keep_alive_start(const struct device *dev)
{
	const struct ip5306_config *cfg = dev->config;
	struct ip5306_data *data = dev->data;

	if (!cfg->wakeup_gpio.port) {
		return 0;
	}

	uint32_t interval_ticks = cfg->keep_alive_interval_ms
				  * 32768UL / 1000UL;
	uint32_t pulse_ticks    = cfg->pulse_width_ms
				  * 32768UL / 1000UL;

	/* GPIOTE may already be initialised by Zephyr shim */
	int err = nrfx_gpiote_init(&gpiote_inst, 0);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("GPIOTE init failed: %d", err);
		return err;
	}

	/* Allocate two GPIOTE channels (same pin: one SET, one CLR) */
	err = nrfx_gpiote_channel_alloc(&gpiote_inst, &data->gpiote_ch_set);
	if (err) {
		LOG_ERR("GPIOTE channel alloc (SET) failed");
		goto channels_full;
	}
	err = nrfx_gpiote_channel_alloc(&gpiote_inst, &data->gpiote_ch_clr);
	if (err) {
		LOG_ERR("GPIOTE channel alloc (CLR) failed");
		goto channels_full;
	}

	/* Output electrical configuration */
	nrfx_gpiote_output_config_t out_cfg = {
		.drive         = NRF_GPIO_PIN_S0S1,
		.input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
		.pull          = NRF_GPIO_PIN_NOPULL,
	};

	/* Channel SET — TASKS_SET: pull KEY high (cycle reset, pulse end) */
	nrfx_gpiote_task_config_t task_cfg_set = {
		.task_ch  = data->gpiote_ch_set,
		.polarity = NRF_GPIOTE_POLARITY_NONE,
		.init_val = NRF_GPIOTE_INITIAL_VALUE_HIGH,
	};
	nrfx_gpiote_output_configure(&gpiote_inst,
				      cfg->wakeup_gpio.pin,
				      &out_cfg, &task_cfg_set);

	/* Channel CLR — TASKS_CLR: pull KEY low (pulse window at cycle end) */
	nrfx_gpiote_task_config_t task_cfg_clr = {
		.task_ch  = data->gpiote_ch_clr,
		.polarity = NRF_GPIOTE_POLARITY_NONE,
		.init_val = NRF_GPIOTE_INITIAL_VALUE_HIGH,
	};
	nrfx_gpiote_output_configure(&gpiote_inst,
				      cfg->wakeup_gpio.pin,
				      &out_cfg, &task_cfg_clr);

	/* Enable via HAL (explicit channel index — nrfx layer tracks
	 * only one channel per pin)
	 */
	nrf_gpiote_task_enable(NRF_GPIOTE, data->gpiote_ch_set);
	nrf_gpiote_task_enable(NRF_GPIOTE, data->gpiote_ch_clr);

	/* ---- RTC2 ---- */
	nrf_rtc_prescaler_set(NRF_RTC2, 0);
	nrf_rtc_cc_set(NRF_RTC2, 0, interval_ticks);
	nrf_rtc_cc_set(NRF_RTC2, 1, interval_ticks - pulse_ticks);
	nrf_rtc_event_enable(NRF_RTC2,
		RTC_EVTENSET_COMPARE0_Msk | RTC_EVTENSET_COMPARE1_Msk);
	nrf_rtc_int_enable(NRF_RTC2, 0); /* no interrupts needed */

	/* ---- PPI ---- */
	uint32_t rtc_cc0_evt = nrf_rtc_event_address_get(
		NRF_RTC2, NRF_RTC_EVENT_COMPARE_0);
	uint32_t rtc_clear   = nrf_rtc_task_address_get(
		NRF_RTC2, NRF_RTC_TASK_CLEAR);
	uint32_t gpio_set    = nrf_gpiote_task_address_get(
		NRF_GPIOTE, nrf_gpiote_set_task_get(data->gpiote_ch_set));
	uint32_t rtc_cc1_evt = nrf_rtc_event_address_get(
		NRF_RTC2, NRF_RTC_EVENT_COMPARE_1);
	uint32_t gpio_clr    = nrf_gpiote_task_address_get(
		NRF_GPIOTE, nrf_gpiote_clr_task_get(data->gpiote_ch_clr));

	data->ppi_cycle_eep = rtc_cc0_evt;
	data->ppi_cycle_tep = rtc_clear;

	err = nrfx_gppi_conn_alloc(rtc_cc0_evt, rtc_clear,
				   &data->ppi_cycle);
	if (err) {
		LOG_ERR("PPI alloc (cycle) failed: %d", err);
		goto ppi_fail;
	}
	nrfx_gppi_ep_attach(gpio_set, data->ppi_cycle);
	nrfx_gppi_conn_enable(data->ppi_cycle);

	data->ppi_pulse_eep = rtc_cc1_evt;
	data->ppi_pulse_tep = gpio_clr;

	err = nrfx_gppi_conn_alloc(rtc_cc1_evt, gpio_clr,
				   &data->ppi_pulse_end);
	if (err) {
		LOG_ERR("PPI alloc (pulse) failed: %d", err);
		goto ppi_fail;
	}
	nrfx_gppi_conn_enable(data->ppi_pulse_end);

	/* Start RTC */
	nrf_rtc_task_trigger(NRF_RTC2, NRF_RTC_TASK_CLEAR);
	nrf_rtc_task_trigger(NRF_RTC2, NRF_RTC_TASK_START);

	LOG_INF("PPI keep-alive: %u ms interval, %u ms pulse",
		cfg->keep_alive_interval_ms, cfg->pulse_width_ms);
	return 0;

ppi_fail:
	nrfx_gpiote_channel_free(&gpiote_inst, data->gpiote_ch_set);
	nrfx_gpiote_channel_free(&gpiote_inst, data->gpiote_ch_clr);
channels_full:
	return err;
}

static void keep_alive_stop(const struct device *dev)
{
	struct ip5306_data *data = dev->data;

	nrf_rtc_task_trigger(NRF_RTC2, NRF_RTC_TASK_STOP);
	nrfx_gppi_conn_free(data->ppi_cycle_eep,
			   data->ppi_cycle_tep,
			   data->ppi_cycle);
	nrfx_gppi_conn_free(data->ppi_pulse_eep,
			   data->ppi_pulse_tep,
			   data->ppi_pulse_end);
	nrfx_gpiote_channel_free(&gpiote_inst, data->gpiote_ch_set);
	nrfx_gpiote_channel_free(&gpiote_inst, data->gpiote_ch_clr);
}

/* ------- CPU timer path (fallback) ----------------------------- */
#else /* !CONFIG_SOC_FAMILY_NORDIC_NRF */

static void cpu_pulse_end_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct ip5306_data *data = CONTAINER_OF(dwork, struct ip5306_data,
						pulse_end_work);
	const struct ip5306_config *cfg = data->dev->config;

	gpio_pin_set_dt(&cfg->wakeup_gpio, 1);

	if (!data->suspended) {
		k_work_reschedule(&data->keep_alive_work,
				  K_MSEC(cfg->keep_alive_interval_ms));
	}
}

static void cpu_keep_alive_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct ip5306_data *data = CONTAINER_OF(dwork, struct ip5306_data,
						keep_alive_work);
	const struct ip5306_config *cfg = data->dev->config;

	gpio_pin_set_dt(&cfg->wakeup_gpio, 0);

	k_work_reschedule(&data->pulse_end_work,
			  K_MSEC(cfg->pulse_width_ms));
}

static int keep_alive_start(const struct device *dev)
{
	struct ip5306_data *data = dev->data;

	k_work_init_delayable(&data->keep_alive_work,
			      cpu_keep_alive_handler);
	k_work_init_delayable(&data->pulse_end_work,
			      cpu_pulse_end_handler);
	k_work_reschedule(&data->keep_alive_work,
			  K_MSEC(((struct ip5306_config *)dev->config)
				  ->keep_alive_interval_ms));
	return 0;
}

static void keep_alive_stop(const struct device *dev)
{
	struct ip5306_data *data = dev->data;

	k_work_cancel_delayable(&data->keep_alive_work);
	k_work_cancel_delayable(&data->pulse_end_work);
}

#endif /* CONFIG_SOC_FAMILY_NORDIC_NRF */

/* ================================================================= *
 *  Suspend / Resume (called by power_mgr or directly)
 * ================================================================= */

void ip5306_suspend(const struct device *dev)
{
	struct ip5306_data *data = dev->data;

	if (data->suspended) {
		return;
	}
	data->suspended = true;
	keep_alive_stop(dev);
}

void ip5306_resume(const struct device *dev)
{
	struct ip5306_data *data = dev->data;

	if (!data->suspended) {
		return;
	}
	data->suspended = false;
	keep_alive_start(dev);
}

/* ================================================================= *
 *  Driver registration
 * ================================================================= */

static int ip5306_init(const struct device *dev)
{
	struct ip5306_data *data = dev->data;
	const struct ip5306_config *cfg = dev->config;

	data->dev = dev;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Configure BOOST always-on to prevent IP5306 auto-shutdown */
	int err = reg_update(dev, REG_SYS_CTL0,
			     SYS_CTL0_BOOST_ALWAYS_ON,
			     SYS_CTL0_BOOST_ALWAYS_ON);
	if (err) {
		LOG_WRN("Failed to set BOOST always-on: %d", err);
	}

	/* Start keep-alive if wakeup-gpios is present */
	if (cfg->wakeup_gpio.port) {
		if (!device_is_ready(cfg->wakeup_gpio.port)) {
			LOG_ERR("Wakeup GPIO port not ready");
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(&cfg->wakeup_gpio,
					     GPIO_OUTPUT_HIGH);
		if (err) {
			LOG_ERR("Wakeup GPIO config failed: %d", err);
			return err;
		}
		err = keep_alive_start(dev);
		if (err) {
			LOG_WRN("Keep-alive start failed: %d", err);
		}
	}

	LOG_INF("IP5306 initialised");
	return 0;
}

/* ================================================================= *
 *  Instance macros
 * ================================================================= */

#define IP5306_CFG_WAKEUP_GPIO(n) \
	GPIO_DT_SPEC_GET_OR(DT_DRV_INST(n), wakeup_gpios, {0})

#define IP5306_INIT(n)							\
	static struct ip5306_data ip5306_data_##n;			\
	static const struct ip5306_config ip5306_cfg_##n = {		\
		.i2c                   = I2C_DT_SPEC_INST_GET(n),	\
		.wakeup_gpio           = IP5306_CFG_WAKEUP_GPIO(n),	\
		.keep_alive_interval_ms =				\
			DT_INST_PROP(n, keep_alive_interval_ms),	\
		.pulse_width_ms        =				\
			DT_INST_PROP(n, pulse_width_ms),		\
	};								\
	DEVICE_DT_INST_DEFINE(n,					\
			      ip5306_init,				\
			      NULL,					\
			      &ip5306_data_##n,			\
			      &ip5306_cfg_##n,			\
			      POST_KERNEL,				\
			      80,					\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(IP5306_INIT)
