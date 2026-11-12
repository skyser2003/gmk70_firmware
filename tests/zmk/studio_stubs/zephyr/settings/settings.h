#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*settings_read_cb)(void *, void *, size_t);
struct settings_handler_static {
    int (*h_set)(const char *, size_t, settings_read_cb, void *);
};
typedef int (*settings_load_direct_cb)(const char *, size_t, settings_read_cb, void *, void *);
struct settings_load_arg {
    const char             *subtree;
    settings_load_direct_cb cb;
    void                   *param;
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
int  settings_name_steq(const char *name, const char *key, const char **next);
