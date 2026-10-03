# Host check, no ESP-IDF: make -C tools/host check
CFLAGS = -std=c11 -O2 -Wall -Wextra -I../../components/gbcore/include -I../../third_party/peanut_gb -I../../include
gb_host: gb_host.c ../../components/gbcore/src/gb_core.c ../../components/gbcore/src/gb_render.c ../../components/gbcore/src/gb_save.c
	$(CC) $(CFLAGS) -o $@ $^
gb_host_threshold: gb_host.c ../../components/gbcore/src/gb_core.c ../../components/gbcore/src/gb_render.c ../../components/gbcore/src/gb_save.c
	$(CC) $(CFLAGS) -DGB_SHADE_MODE=1 -o $@ $^
check: gb_host gb_host_threshold
	../fetch_roms.sh >/dev/null
	./gb_host ../../tests/roms/libbet.gb ../../docs/libbet 600
	./gb_host ../../tests/roms/dmg-acid2.gb ../../docs/acid2 120 60 120
	./gb_host_threshold ../../tests/roms/libbet.gb ../../docs/libbet-threshold 600 600
	./gb_host_threshold ../../tests/roms/dmg-acid2.gb ../../docs/acid2-threshold 120 120
test_save: test_save.c ../../components/gbcore/src/gb_save.c
	$(CC) $(CFLAGS) -o $@ $^
test_xbox: test_xbox.c ../../components/pad/src/xbox_report.c
	$(CC) $(CFLAGS) -I../../components/pad/include -o $@ $^
test: test_save test_xbox
	./test_save
	./test_xbox
clean:
	rm -f gb_host gb_host_threshold test_save test_xbox
