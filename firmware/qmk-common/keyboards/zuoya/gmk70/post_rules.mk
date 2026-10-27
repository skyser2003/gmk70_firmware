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
ifeq ($(strip $(GMK70_VIAL_TRIAL)), yes)
    OPT_DEFS += -DGMK70_VIAL_TRIAL
    SRC += update/trial.c update/trial_boot.c update/trial_watchdog.c
    SRC += update/trial_peer.c
    ifeq ($(strip $(GMK70_TRIAL_RUNTIME)), yes)
        OPT_DEFS += -DGMK70_TRIAL_RUNTIME
        SRC += update/trial_runtime.c update/trial_deadline.c update/trial_fault.S
    else
        SRC += update/trial_platform.c update/trial_startup.S update/trial_role.c
    endif
    LDFLAGS += -Wl,--defsym=GMK70_TRIAL_RAM_RESERVE=1
    LDFLAGS += -Wl,--wrap=transport_master_if_connected -Wl,--wrap=transaction_rpc_exec
endif
include $(GMK70_PLATFORM_DIR)/compat/platform_rules.mk
include $(KEYBOARD_PATH_1)/wls/wls.mk
include $(GMK70_COMMON_DIR)/drivers/wireless/wireless.mk
