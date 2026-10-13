# Framework integration for official QMK, including the fixed-keymap build.
SRC += rgb_integration.c stock_eeprom.c
LDFLAGS += -Wl,--wrap=rgb_matrix_set_color
LDFLAGS += -Wl,--wrap=nvm_eeconfig_is_enabled -Wl,--wrap=nvm_eeconfig_is_disabled
LDFLAGS += -Wl,--wrap=nvm_eeconfig_enable -Wl,--wrap=nvm_eeconfig_disable
LDFLAGS += -Wl,--wrap=nvm_eeconfig_read_rgb_matrix -Wl,--wrap=nvm_eeconfig_update_rgb_matrix
LDFLAGS += -Wl,--wrap=nvm_eeconfig_read_rgblight -Wl,--wrap=nvm_eeconfig_update_rgblight
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    SRC += update/eeprom_query.c
endif
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += stock_via.c
    LDFLAGS += -Wl,--wrap=nvm_via_read_magic -Wl,--wrap=nvm_via_update_magic
endif
