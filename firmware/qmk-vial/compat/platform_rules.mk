# Framework integration for the independently pinned Vial fork.
SRC += rgb_integration.c vial_adapter.c manual_keycodes.c
LDFLAGS += -Wl,--wrap=rgb_matrix_set_color
LDFLAGS += -Wl,--wrap=raw_hid_receive
LDFLAGS += -Wl,--wrap=housekeeping_task
LDFLAGS += -Wl,--wrap=nvm_eeconfig_is_enabled -Wl,--wrap=nvm_eeconfig_enable
ifeq ($(strip $(GMK70_RADIO_DIAGNOSTICS)), yes)
    LDFLAGS += -Wl,--wrap=via_init -Wl,--wrap=via_eeprom_is_valid
endif
