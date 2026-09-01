/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <caf/click_detector.h>
#include <caf/key_id.h>

/* single-include guard */
const struct {} click_detector_def_include_once;

static const struct click_detector_config click_detector_config[] = {
	{
		.key_id = KEY_ID(3, 0),  /* 旋钮按键：COL3, ROW0 */
		.consume_button_event = false,
	},
};
