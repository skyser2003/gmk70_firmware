#pragma once
#define POST_KERNEL 0
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 0
#define SYS_INIT(fn, level, priority) static void *init_ref __attribute__((used)) = (void *)&fn
