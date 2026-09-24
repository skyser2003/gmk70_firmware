RGB_MATRIX_CUSTOM_USER = yes
SRC += split_role.c
SRC += stock_keycodes.c
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += stock_via.c
endif
include keyboards/zuoya/gmk70/rgb_record/rgb_record.mk
include keyboards/zuoya/gmk70/wls/wls.mk
include drivers/wireless/wireless.mk
