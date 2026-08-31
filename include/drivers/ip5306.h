/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _DRIVERS_IP5306_H_
#define _DRIVERS_IP5306_H_

#include <stdbool.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief IP5306 charging state. */
enum ip5306_charge_state {
	IP5306_CHARGE_DISABLED,
	IP5306_CHARGE_ACTIVE,
	IP5306_CHARGE_FULL,
};

/** @brief Get charging state. */
int ip5306_charge_status(const struct device *dev,
			  enum ip5306_charge_state *state);

/** @brief Quick check: is charging active? */
int ip5306_is_charging(const struct device *dev, bool *charging);

/** @brief Quick check: is battery full? */
int ip5306_is_full(const struct device *dev, bool *full);

/** @brief Manually trigger one KEY keep-alive pulse. */
int ip5306_wakeup(const struct device *dev);

/** @brief Suspend keep-alive (called on power_down). */
void ip5306_suspend(const struct device *dev);

/** @brief Resume keep-alive (called on wake_up). */
void ip5306_resume(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_IP5306_H_ */
