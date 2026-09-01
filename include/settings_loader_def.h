/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Settings loader configuration — defines which modules must be
 * initialized before settings_load() is called.
 */

const struct {} settings_loader_def_include_once;

#include <caf/events/module_state_event.h>

static inline void get_req_modules(struct module_flags *mf)
{
	module_flags_set_bit(mf, MODULE_IDX(main));
#ifdef CONFIG_CAF_BLE_STATE
	module_flags_set_bit(mf, MODULE_IDX(ble_state));
#endif
}
