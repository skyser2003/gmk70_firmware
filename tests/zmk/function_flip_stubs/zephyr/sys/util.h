/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#define CONTAINER_OF(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
