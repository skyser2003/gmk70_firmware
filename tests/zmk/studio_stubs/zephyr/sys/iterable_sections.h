/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define STRUCT_SECTION_FOREACH(type, iterator) for (struct type *iterator = settings_test_ids; iterator < settings_test_ids + 2; ++iterator)
