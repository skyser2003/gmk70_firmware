/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../../radio_behavior_stubs/zephyr/device.h"
#undef IS_ENABLED
#define IS_ENABLED(value) value
