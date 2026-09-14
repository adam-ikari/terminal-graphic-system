.PHONY: all init build test run clean format

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

clean:
	rm -rf build

format:
	clang-format -i src/**/*.c src/**/*.h
