RGB_MATRIX_CUSTOM_USER = yes
DEBOUNCE_TYPE = sym_defer_pk
SRC += split_role.c
SRC += stock_keycodes.c
SRC += battery_indicator.c battery_indicator_qmk.c
LDFLAGS += -Wl,--wrap=ws2812_flush
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += stock_via.c
endif
include keyboards/zuoya/gmk70/rgb_record/rgb_record.mk
include keyboards/zuoya/gmk70/wls/wls.mk
include drivers/wireless/wireless.mk
