include $(GMK70_PLATFORM_DIR)/keymaps/release/rules.mk
OPT_DEFS += -DGMK70_SPLIT_DIAGNOSTICS -DGMK70_DEBUG_USB_BOOTLOADER
SRC += split_diagnostics.c split_diagnostics_qmk.c
LDFLAGS += -Wl,--wrap=transaction_rpc_exec
ifeq ($(strip $(GMK70_SPLIT_UPDATE)), yes)
    OPT_DEFS += -DGMK70_PERSISTENT_DIAGNOSTICS
    SRC += sleep_log.c sleep_log_qmk.c update/sleep_log_query.c
    LDFLAGS += -Wl,--wrap=flash_read_range -Wl,--wrap=flash_write_range
    LDFLAGS += -Wl,--wrap=flash_erase_sector -Wl,--wrap=flash_erase_block
endif
