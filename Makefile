# make setup (once: ESP-IDF cache), make build, make flash-image. See README.md.
IDF := tools/idf/idf.sh

.PHONY: setup build flash-image host-check clean
setup:
	$(IDF) setup

# build/dist/gameboy-merged.bin (write at 0x0)
build:
	$(IDF) tools/idf/build.sh

flash-image: build
	@ls -l build/dist/gameboy-merged.bin

host-check:
	$(MAKE) -C tools/host check

clean:
	rm -rf build sdkconfig sdkconfig.old
