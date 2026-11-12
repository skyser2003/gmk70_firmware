#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
uv run --no-sync --project tools/build-support python tests/zmk/power_pm_native.py \
    .work/zmk/tests/power_pm_native.c .work/zmk/tests/power_activity_native.c
sources=(tests/zmk/power_native_test.c firmware/zmk/power/power.c
    firmware/zmk/power/core.c firmware/zmk/power/protocol.c firmware/zmk/power/coordinator.c
    firmware/zmk/wired/wired_protocol.c .work/zmk/tests/power_pm_native.c
    .work/zmk/tests/power_activity_native.c)
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_GMK70_EVENT_IDLE=1 \
    -DCONFIG_GMK70_BATTERY=1 -DCONFIG_GMK70_RADIO=1 -DCONFIG_GMK70_CONTROLS=1 \
    -DCONFIG_GMK70_FUNCTION_FLIP=1 -DCONFIG_GMK70_NKRO=1 -DCONFIG_GMK70_WB32_RGB=1 \
    -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -DCONFIG_ZMK_IDLE_TIMEOUT=30000 \
    -DCONFIG_ZMK_POINTING=1 \
    -Itests/zmk/power_native_stubs -Itests/zmk/stubs -Idependencies/zmk/app/include -Idependencies/zephyr/include -Ifirmware/zmk/update \
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update -Ifirmware/qmk-common/drivers/wireless -Ifirmware/zmk/wired \
    "${sources[@]}" -Wl,--wrap=zmk_pm_suspend_devices -Wl,--wrap=zmk_pm_resume_devices \
    -Wl,--wrap=zmk_pm_soft_off \
    -Wl,--wrap=zmk_usb_get_conn_state -o .work/zmk/tests/power-native
for scenario in disconnected maintenance-stop gates peer-traffic native-stop critical manual new-input usb-shallow right-commit right-rx right-cancel \
    usb-tick usb-pendsv new-critical-input pm-error pm-activity-race usb-blocked-manual \
    fallback-local-wake fallback-rx-wake usb-unplug-configured usb-unplug-suspended usb-connected-hid; do
    .work/zmk/tests/power-native "$scenario"
done
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DTEST_NATIVE_RGB=1 -DCONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=0 \
    -DCONFIG_GMK70_BATTERY=1 -DCONFIG_GMK70_RADIO=1 -DCONFIG_GMK70_CONTROLS=1 \
    -DCONFIG_GMK70_FUNCTION_FLIP=1 -DCONFIG_GMK70_NKRO=1 -DCONFIG_GMK70_WB32_RGB=1 \
    -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -DCONFIG_ZMK_IDLE_TIMEOUT=30000 -DCONFIG_ZMK_POINTING=1 \
    -Itests/zmk/power_native_stubs -Itests/zmk/stubs -Idependencies/zmk/app/include \
    -Itests/zmk/function_flip_stubs -Idependencies/zephyr/include -Ifirmware/zmk/update \
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update -Ifirmware/qmk-common/drivers/wireless -Ifirmware/zmk/wired \
    "${sources[@]}" firmware/zmk/lighting/rgb_settings.c tests/zmk/power_rgb_fixture.c \
    -Wl,--wrap=zmk_pm_suspend_devices -Wl,--wrap=zmk_pm_resume_devices -Wl,--wrap=zmk_pm_soft_off \
    -Wl,--wrap=zmk_usb_get_conn_state -Wl,--wrap=settings_save_one \
    -Wl,--wrap=zmk_rgb_underglow_set_hsb -Wl,--wrap=zmk_rgb_underglow_on -Wl,--wrap=zmk_rgb_underglow_off \
    -Wl,--wrap=zmk_rgb_underglow_toggle -o .work/zmk/tests/power-critical-settings
.work/zmk/tests/power-critical-settings critical-settings

cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_GMK70_EVENT_IDLE=1 -DCONFIG_GMK70_RETAINED_SLEEP=1 \
    -DCONFIG_GMK70_BATTERY=1 -DCONFIG_GMK70_RADIO=1 -DCONFIG_GMK70_CONTROLS=1 \
    -DCONFIG_GMK70_FUNCTION_FLIP=1 -DCONFIG_GMK70_NKRO=1 -DCONFIG_GMK70_WB32_RGB=1 \
    -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -DCONFIG_ZMK_IDLE_TIMEOUT=30000 \
    -DCONFIG_ZMK_POINTING=1 \
    -Itests/zmk/power_native_stubs -Itests/zmk/stubs -Idependencies/zmk/app/include -Idependencies/zephyr/include -Ifirmware/zmk/update \
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update -Ifirmware/qmk-common/drivers/wireless -Ifirmware/zmk/wired \
    "${sources[@]}" -Wl,--wrap=zmk_pm_suspend_devices -Wl,--wrap=zmk_pm_resume_devices \
    -Wl,--wrap=zmk_pm_soft_off \
    -Wl,--wrap=zmk_usb_get_conn_state -o .work/zmk/tests/power-retained
for scenario in retained retained-defer retained-unavailable disconnected-reconnect; do
    .work/zmk/tests/power-retained "$scenario"
done
