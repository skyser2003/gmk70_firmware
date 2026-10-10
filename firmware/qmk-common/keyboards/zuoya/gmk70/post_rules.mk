DEBOUNCE_TYPE = sym_defer_g
SRC += split_role.c
SRC += stock_keycodes.c
SRC += stock_battery.c
SRC += stock_power.c
SRC += stock_lighting.c
ifneq ($(strip $(VIAL_ENABLE)), yes)
SRC += stock_eeprom.c
LDFLAGS += -Wl,--wrap=nvm_eeconfig_is_enabled -Wl,--wrap=nvm_eeconfig_is_disabled
LDFLAGS += -Wl,--wrap=nvm_eeconfig_enable -Wl,--wrap=nvm_eeconfig_disable
LDFLAGS += -Wl,--wrap=nvm_eeconfig_read_rgb_matrix -Wl,--wrap=nvm_eeconfig_update_rgb_matrix
LDFLAGS += -Wl,--wrap=nvm_eeconfig_read_rgblight -Wl,--wrap=nvm_eeconfig_update_rgblight
endif
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    OPT_DEFS += -DGMK70_SPLIT_UPDATE
    SRC += update/image.c update/protocol.c update/wb32_flash.c update/qmk_update.c
    ifneq ($(strip $(VIAL_ENABLE)), yes)
        SRC += update/eeprom_query.c
    endif
endif
ifeq ($(strip $(VIAL_ENABLE)), yes)
    SRC += vial_adapter.c
    LDFLAGS += -Wl,--wrap=raw_hid_receive
    LDFLAGS += -Wl,--wrap=nvm_eeconfig_is_enabled -Wl,--wrap=nvm_eeconfig_enable
else ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += stock_via.c
    LDFLAGS += -Wl,--wrap=nvm_via_read_magic -Wl,--wrap=nvm_via_update_magic
endif
include keyboards/zuoya/gmk70/wls/wls.mk
include drivers/wireless/wireless.mk
