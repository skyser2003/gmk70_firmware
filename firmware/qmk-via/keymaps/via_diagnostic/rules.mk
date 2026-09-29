include keyboards/zuoya/gmk70/keymaps/via/rules.mk
OPT_DEFS += -DGMK70_BATTERY_DIAGNOSTICS
SRC += battery_diagnostics.c battery_output_probe.c battery_output_probe_spi.c
LDFLAGS += -Wl,--wrap=spiSend
