#pragma once
#define SYS_INIT(fn, level, priority) static void *const init_ref __attribute__((used)) = (void *)&fn
