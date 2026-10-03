# trmnl-og-gameboy

A separate firmware for the TRMNL OG (ESP32-C3, 800x480 e-paper): a Game Boy emulator on e-ink. Proof of concept:
is it playable at e-ink frame rates? Flash it like any image; flash tiny-paper again to go back.

| | |
|---|---|
| Emulator | Peanut-GB (MIT, `third_party/peanut_gb`), no sound |
| Panel | OG driver from tiny-paper (GPL-3.0, `components/epd`, source in SOURCE.md) |
| Picture | 160x144 at 3x (480x432), centred, Bayer 4x4 dither to 1 bit |
| Refresh | partial refresh of the changed rows, full refresh every 20 partials (`menuconfig` > Game Boy) |
| Input | the one button: press = A, hold 1 s = Start. Interface `gb_input.h` for a BLE gamepad later |
| Games | homebrew or games you own, never in this repo; loaded over USB |

## Build

| Step | Command |
|---|---|
| ESP-IDF cache (once, ~3 GB, shared with tiny-paper) | `make setup` |
| Firmware | `make build` -> `build/dist/gameboy-merged.bin` (write at 0x0), ~210 KB app in a 1.5 MB slot |
| Host check (gcc only) | `make host-check` -> PNGs in `docs/` |
| GEN2 panel variant | set `GB_EPD_GEN2` in `idf.py menuconfig` (default GDEY075T7) |

## Flash and play

| Step | How |
|---|---|
| Flash | tiny-paper web app, Flash page: choose `build/dist/gameboy-merged.bin` at offset `0x0` in the file/offset section. Or `python -m esptool --chip esp32c3 write_flash 0x0 build/dist/gameboy-merged.bin` |
| ROM | `tools/fetch_roms.sh` then `tools/load_rom.sh tests/roms/libbet.gb [/dev/ttyACM0]` (writes at 0x190000, the `rom` partition, 1 MB). On the Flash page: the same .gb at offset `0x190000` |
| Measure | serial monitor, one line per second: `gb fps_emu=<n> fps_panel=<n> partial_ms=<n> full_every=<n>` |
| Back to tiny-paper | flash a tiny-paper image at 0x0 (Flash page, install); its partition table replaces this one |

Without a valid ROM the firmware prints an error every 5 s. Flashing 0x0 does not touch the ROM.

## Expected

Not measured on hardware yet (that is the point). Emulation: 59.7 fps wanted, the C3 may reach less. Panel: a partial refresh
of the OTP waveform takes about 1 to 1.7 s, so expect about 1 picture per second, a full refresh (flashes, several
seconds) every 20. Test ROMs: `tests/ROMS.md`. Host snapshots: `docs/libbet-*.png`, `docs/acid2-*.png`.
