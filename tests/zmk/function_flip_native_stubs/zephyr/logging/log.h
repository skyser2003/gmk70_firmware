/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
extern unsigned function_flip_native_errors;
void            function_flip_native_debug(const char *format);
#define LOG_MODULE_DECLARE(...)
#define LOG_DBG(format, ...) function_flip_native_debug(format)
#define LOG_ERR(...) (++function_flip_native_errors)
