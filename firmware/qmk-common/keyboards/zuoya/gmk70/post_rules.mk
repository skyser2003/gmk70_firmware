DEBOUNCE_TYPE = sym_defer_g
SRC += split_role.c
SRC += stock_keycodes.c
SRC += stock_battery.c
SRC += stock_power.c
SRC += stock_lighting.c
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    OPT_DEFS += -DGMK70_SPLIT_UPDATE
    SRC += update/image.c update/protocol.c update/wb32_flash.c update/qmk_update.c
endif
include $(GMK70_PLATFORM_DIR)/compat/platform_rules.mk
include $(KEYBOARD_PATH_1)/wls/wls.mk
include $(GMK70_COMMON_DIR)/drivers/wireless/wireless.mk
