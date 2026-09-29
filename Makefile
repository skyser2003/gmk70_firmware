build: build-qmk-via
build-qmk-via:
	bash ./scripts/build.sh qmk via
build-diagnostic:
	bash ./scripts/build.sh qmk via_diagnostic
build-default: build-qmk-default
build-qmk-default:
	bash ./scripts/build.sh qmk default
build-vial:
	bash ./scripts/build.sh vial
build-zmk:
	bash ./scripts/build.sh zmk
clean:
	bash ./scripts/clean.sh
rebuild: clean
	$(MAKE) build
format:
	bash ./scripts/format.sh
test:
	bash ./scripts/test.sh
.PHONY: build build-qmk-via build-diagnostic build-default build-qmk-default build-vial build-zmk clean rebuild format test

setup:
	bash ./scripts/setup.sh
check-tools:
	bash ./scripts/check-tools.sh
.PHONY: setup check-tools
