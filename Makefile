build:
	bash ./scripts/build.sh

build-default:
	km=default bash ./scripts/build.sh

build-mine:
	km=mine bash ./scripts/build.sh

clean:
	bash ./scripts/clean.sh

rebuild: clean
	$(MAKE) build

format:
	find keyboards -name "*.[ch]" | xargs clang-format -i

test:
	bash ./scripts/test.sh
.PHONY: build build-default build-mine clean rebuild format test
