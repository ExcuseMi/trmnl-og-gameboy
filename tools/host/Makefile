# Host check, no ESP-IDF: make -C tools/host check
CFLAGS = -std=c11 -O2 -Wall -Wextra -I../../components/gbcore/include -I../../third_party/peanut_gb -I../../include
gb_host: gb_host.c ../../components/gbcore/src/gb_core.c ../../components/gbcore/src/gb_render.c ../../components/gbcore/src/gb_save.c
	$(CC) $(CFLAGS) -o $@ $^
check: gb_host
	../fetch_roms.sh >/dev/null
	./gb_host ../../tests/roms/libbet.gb ../../docs/libbet 600
	./gb_host ../../tests/roms/dmg-acid2.gb ../../docs/acid2 120 60 120
test_save: test_save.c ../../components/gbcore/src/gb_save.c
	$(CC) $(CFLAGS) -o $@ $^
test: test_save
	./test_save
clean:
	rm -f gb_host test_save
