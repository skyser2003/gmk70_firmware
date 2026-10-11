# Build maintained GMK70 sources directly against an unmodified framework.
ifndef GMK70_PROJECT_ROOT
    $(error GMK70_PROJECT_ROOT must point to the maintained repository)
endif

GMK70_COMMON_DIR := $(GMK70_PROJECT_ROOT)/firmware/qmk-common
GMK70_BOARD_DIR := $(GMK70_COMMON_DIR)/keyboards/zuoya/gmk70
GMK70_PLATFORM_DIR := $(GMK70_PROJECT_ROOT)/firmware/qmk-$(if $(filter vial,$(GMK70_PLATFORM)),vial,via)

# override survives the fixed keyboards/ paths assigned by upstream Make.
override KEYBOARD_PATH_1 := $(GMK70_BOARD_DIR)
override KEYBOARD_PATH_2 := $(GMK70_COMMON_DIR)/keyboards/zuoya
override KEYBOARD_PATH_3 := $(GMK70_COMMON_DIR)/keyboards/.
override KEYBOARD_PATH_4 := $(KEYBOARD_PATH_3)
override KEYBOARD_PATH_5 := $(KEYBOARD_PATH_3)
override KEYMAP_PATH := $(if $(filter default,$(KEYMAP)),$(GMK70_BOARD_DIR)/keymaps/default,$(GMK70_PLATFORM_DIR)/keymaps/$(KEYMAP))
override KEYMAP_C := $(KEYMAP_PATH)/keymap.c
VPATH += $(GMK70_PLATFORM_DIR)/compat $(GMK70_BOARD_DIR)/keymaps

# This Vial pin skips keymap-rule discovery when KEYMAP_PATH is supplied.
ifeq ($(GMK70_PLATFORM),vial)
    include $(KEYMAP_PATH)/rules.mk
endif

include builddefs/build_keyboard.mk

# Metadata also reads config/C headers. Regenerate after edits or source-file
# removal, without discarding the compiled objects for unchanged sources.
GMK70_METADATA_INPUTS := $(wildcard $(GMK70_BOARD_DIR)/*.json $(GMK70_BOARD_DIR)/*.h $(GMK70_BOARD_DIR)/*.c $(KEYMAP_PATH)/*.mk $(KEYMAP_PATH)/*.h)
GMK70_METADATA_INPUTS += $(GMK70_PROJECT_ROOT)/tools/build-support/qmk_generate.py $(GMK70_PROJECT_ROOT)/tools/build-support/external.mk
GMK70_METADATA_INPUTS += $(dir $(BUILD_DIR))gmk70-source-origins.json
$(addprefix $(INTERMEDIATE_OUTPUT)/src/,info_config.h default_keyboard.h default_keyboard.c info_deps.d): $(GMK70_METADATA_INPUTS)

# Upstream does not track linker-script contents when linking cached objects.
$(BUILD_DIR)/$(TARGET).elf: $(LDSCRIPT)

# Upstream's bin recipes also copy the image into the framework root. Request
# this more specific output-only rule instead, keeping dependencies pristine.
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	@$(SILENT) || printf "$(MSG_BIN) $@" | $(AWK_CMD)
	$(eval CMD=$(BIN) $< $@)
	@$(BUILD_CMD)
