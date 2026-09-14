.PHONY: all init build test run run-xvfb clean format

all: build

init:
	git submodule update --init --recursive
	@mkdir -p build
	cd build && cmake ..

build:
	cmake --build build -j$$(nproc)

test: build
	cd build && ctest --output-on-failure

run: build
	./build/tgs-compositor ./build/simple_form

run-xvfb:
	./scripts/run-xvfb.sh xvfb-shot.png

clean:
	rm -rf build

format:
	clang-format -i src/**/*.c src/**/*.h
