RGB_MATRIX_CUSTOM_USER = yes
DEBOUNCE_TYPE = sym_defer_g
SRC += split_role.c
SRC += stock_keycodes.c
SRC += stock_battery.c
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    OPT_DEFS += -DGMK70_SPLIT_UPDATE
    SRC += update/image.c update/protocol.c update/wb32_flash.c update/qmk_update.c
endif
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += stock_via.c
endif
include keyboards/zuoya/gmk70/rgb_record/rgb_record.mk
include keyboards/zuoya/gmk70/wls/wls.mk
include drivers/wireless/wireless.mk
