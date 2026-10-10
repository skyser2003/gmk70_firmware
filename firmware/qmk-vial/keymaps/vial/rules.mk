VIA_ENABLE = yes
VIAL_ENABLE = yes
VIALRGB_ENABLE = yes
ENCODER_MAP_ENABLE = yes
# Native Vial settings include Auto Shift, tap/hold, one-shot and mouse tuning.
QMK_SETTINGS = yes
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
KEY_OVERRIDE_ENABLE = yes
CAPS_WORD_ENABLE = yes
LAYER_LOCK_ENABLE = yes
# Also enables Vial's dynamic alternate-repeat rules.
REPEAT_KEY_ENABLE = yes
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    OPT_DEFS += -DGMK70_VIAL_UPDATE
endif
