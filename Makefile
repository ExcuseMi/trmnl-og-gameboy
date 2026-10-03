# make setup (once: ESP-IDF cache), make build, make flash-image. See README.md.
IDF := tools/idf/idf.sh

.PHONY: setup build web flash-image host-check clean
setup:
	$(IDF) setup

# build/dist/gameboy-merged.bin (write at 0x0). GB_SHADE_MODE=bayer (default) or threshold
GB_SHADE_MODE ?= bayer
build: web
	$(IDF) tools/idf/build.sh $(GB_SHADE_MODE)
	cp build/dist/gameboy-merged.bin tools/web/

# tools/web/offsets.json for the browser page (tools/web/index.html), from partitions.csv
web:
	python3 tools/part_offset.py partitions.csv --json > tools/web/offsets.json

flash-image: build
	@ls -l build/dist/gameboy-merged.bin

host-check:
	$(MAKE) -C tools/host check

clean:
	rm -rf build sdkconfig sdkconfig.old
