/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DRIVERS_IP5306_H_
#define _DRIVERS_IP5306_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief IP5306 charge/full status. */
struct ip5306_status {
	/** True if battery is being charged. */
	bool charging;
	/** True if battery is full. */
	bool full;
};

/**
 * @brief Initialize the IP5306 PMIC driver and start hardware keepalive.
 *
 * @return 0 on success, negative error code on failure.
 */
int ip5306_init(void);

/**
 * @brief Read IP5306 charging/full status.
 *
 * @param[out] status Status output.
 *
 * @return 0 on success, negative error code on failure.
 */
int ip5306_get_status(struct ip5306_status *status);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_IP5306_H_ */
