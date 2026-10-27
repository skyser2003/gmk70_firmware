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
.PHONY: test-zmk

build-zmk-right-trial:
	bash ./scripts/build.sh zmk right-trial
.PHONY: build-zmk-right-trial
