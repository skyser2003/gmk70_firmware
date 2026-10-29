build: build-release build-debug
build-release:
	bash ./scripts/build.sh qmk release
build-debug:
	bash ./scripts/build.sh qmk debug
build-updater: build-updater-release build-updater-debug
build-updater-release:
	bash ./scripts/build.sh qmk release updater
build-updater-debug:
	bash ./scripts/build.sh qmk debug updater
.PHONY: build-updater build-updater-release build-updater-debug
build-qmk-via: build-release
build-diagnostic: build-debug
build-default: build-qmk-default
build-qmk-default:
	bash ./scripts/build.sh qmk default
build-vial:
	bash ./scripts/build.sh vial
build-vial-updater:
	bash ./scripts/build.sh vial vial updater
build-vial-unlocked-updater:
	bash ./scripts/build.sh vial vial unlocked-updater
build-vial-radio-diagnostic:
	bash ./scripts/build.sh vial vial radio-diagnostic-updater
.PHONY: build-vial-radio-diagnostic
build-zmk:
	bash ./scripts/build.sh zmk left-trial
clean:
	bash ./scripts/clean.sh
rebuild: clean
	$(MAKE) build
format:
	bash ./scripts/format.sh
test:
	bash ./scripts/test.sh
.PHONY: build build-release build-debug build-qmk-via build-diagnostic build-default build-qmk-default build-vial build-vial-updater build-vial-unlocked-updater build-zmk clean rebuild format test

setup:
	bash ./scripts/setup.sh
check-tools:
	bash ./scripts/check-tools.sh
.PHONY: setup check-tools

setup-vial:
	bash ./scripts/setup_vial.sh
.PHONY: setup-vial

build-vial-trial-updater:
	bash ./scripts/build.sh vial vial trial-updater
build-vial-unlocked-trial-updater:
	bash ./scripts/build.sh vial vial unlocked-trial-updater
build-vial-watchdog-qualification:
	GMK70_TRIAL_RUNTIME=yes bash ./scripts/build.sh vial vial unlocked-trial-updater
build-zmk-trial-fixtures:
	bash ./scripts/build_zmk_trial_fixtures.sh
.PHONY: build-vial-trial-updater build-vial-unlocked-trial-updater build-vial-watchdog-qualification build-zmk-trial-fixtures

setup-zmk:
	bash ./scripts/setup_zmk.sh
.PHONY: setup-zmk

test-zmk:
	bash ./tests/zmk/run_main_return_tests.sh
	bash ./tests/zmk/run_usb_dc_tests.sh
	bash ./tests/zmk/run_soc_clock_tests.sh
	bash ./tests/zmk/run_wired_tests.sh
	bash ./tests/zmk/run_update_platform_tests.sh
	bash ./tests/zmk/run_receiver_tests.sh
	bash ./tests/zmk/run_studio_settings_tests.sh
	bash ./tests/zmk/run_studio_feature_tests.sh
	bash ./tests/zmk/run_lighting_tests.sh
	bash ./tests/zmk/run_pixels_tests.sh
	bash -c 'set -euo pipefail; source ./scripts/project.sh; uv run --offline --frozen --project firmware/zmk/tools python -m unittest discover -s tests/zmk -p "test_*.py" -v'
.PHONY: test-zmk

build-zmk-right-trial:
	bash ./scripts/build.sh zmk right-trial
.PHONY: build-zmk-right-trial

build-zmk-wired-updater:
	bash ./scripts/build.sh zmk wired-updater
build-zmk-wired-update-test:
	bash ./scripts/build.sh zmk wired-update-test
build-zmk-wired-qualification:
	bash ./scripts/build.sh zmk wired-qualification
.PHONY: build-zmk-wired-updater build-zmk-wired-update-test build-zmk-wired-qualification

build-zmk-wired-studio-updater:
	bash ./scripts/build_zmk.sh wired-studio-updater
build-zmk-wired-studio-update-test:
	bash ./scripts/build_zmk.sh wired-studio-update-test
build-zmk-wired-peripheral:
	bash ./scripts/build_zmk.sh wired-peripheral
.PHONY: build-zmk-wired-studio-update-test build-zmk-wired-peripheral
.PHONY: build-zmk-wired-studio-updater

build-zmk-wired-studio-handoff:
	bash ./scripts/build_zmk.sh wired-studio-handoff
build-zmk-wired-studio-handoff-test:
	bash ./scripts/build_zmk.sh wired-studio-handoff-test
.PHONY: build-zmk-wired-studio-handoff build-zmk-wired-studio-handoff-test

build-zmk-wired-studio-watchdog-rollback:
	bash ./scripts/build_zmk.sh wired-studio-watchdog-rollback
build-zmk-wired-studio-watchdog-rollback-test:
	bash ./scripts/build_zmk.sh wired-studio-watchdog-rollback-test
.PHONY: build-zmk-wired-studio-watchdog-rollback build-zmk-wired-studio-watchdog-rollback-test
