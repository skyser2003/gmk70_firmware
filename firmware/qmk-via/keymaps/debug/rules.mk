include keyboards/zuoya/gmk70/keymaps/release/rules.mk
OPT_DEFS += -DGMK70_SPLIT_DIAGNOSTICS -DGMK70_DEBUG_USB_BOOTLOADER
SRC += split_diagnostics.c split_diagnostics_qmk.c
LDFLAGS += -Wl,--wrap=transaction_rpc_exec
