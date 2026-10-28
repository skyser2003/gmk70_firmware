#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
struct settings_load_arg {
    const char *subtree;
};
struct settings_store;
struct settings_store_itf {
    int (*csi_load)(struct settings_store *, const struct settings_load_arg *);
    int (*csi_save)(struct settings_store *, const char *, const char *, size_t);
};
struct settings_store {
    const struct settings_store_itf *cs_itf;
};
void settings_src_register(struct settings_store *);
void settings_dst_register(struct settings_store *);
int  settings_call_set_handler(const char *, size_t, settings_read_cb, void *, const struct settings_load_arg *);
