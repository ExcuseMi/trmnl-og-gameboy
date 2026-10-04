# trmnl-og-gameboy

A separate firmware for the TRMNL OG (ESP32-C3, 800x480 e-paper): a Game Boy emulator on e-ink, played with an Xbox
controller over Bluetooth. Proof of concept: is it playable at e-ink frame rates?

| | |
|---|---|
| Emulator | Peanut-GB (MIT, `third_party/peanut_gb`), no sound, MBC1/2/3/5 |
| Panel | OG driver from tiny-paper (GPL-3.0, `components/epd`, source in SOURCE.md) |
| Picture | 160x144 at 3x (480x432), centred, 1 bit: Bayer 4x4 dither or plain threshold (`GB_SHADE_MODE`) |
| Refresh | partial refresh of the changed rows, back to back; full refresh every 300 partials or fewer (panel speed below) |
| Input | Xbox controller over Bluetooth LE; the OG button too: press = A, hold 1 s = Start |
| Saves | battery RAM (up to 32 KB) in the `save` flash partition |
| Save states | full snapshot of the emulator every 2 minutes (`GB_STATE_EVERY_S`, 0 = off), resumed at boot |
| Games | homebrew or games you own, never in this repo; loaded over USB |

## Play (owner)

| Step | How |
|---|---|
| 1. Firmware | Chrome or Edge: https://excusemi.github.io/trmnl-og-gameboy/ , USB cable, `Flash firmware` (latest build of main) |
| 2. Game | same page: choose the .gb file (up to 1 MB), `Upload ROM` |
| 3. Controller | the panel shows `Hold the pair button on the controller`: switch the controller on, hold its pair button (top edge, next to USB) about 3 s until the Xbox logo blinks fast. The line disappears when it is connected |
| Later | switch the controller on: it reconnects by itself (bond kept in flash, also across a firmware flash) |
| Back to tiny-paper | flash a tiny-paper image at 0x0; its partition table replaces this one |

| Xbox controller | Works |
|---|---|
| Model 1914 (Series X/S, Share button) | yes |
| Model 1708 (One S, 2016) | yes, after the firmware update to 5.x in the Xbox Accessories app (Windows or Xbox) |
| Models 1537, 1697 (first Xbox One) | no: no Bluetooth |
| Elite 2 (1797), Adaptive | not tested; same report format after their 5.x update |

| Controller | Game Boy |
|---|---|
| D-pad, left stick | D-pad |
| A, B | A, B |
| Menu (three lines) | Start |
| View (two squares) | Select |
| RB / LB | panel speed: next / previous preset. Shown as `speed N` for 2 s, kept across restarts |
| Y | full refresh now (flashes once, clears the ghosting) |

Panel speed presets (`main/gb_speed.c`). Faster ones drive each changed pixel shorter: paler, more ghosting, a full
refresh more often. Step up until it no longer looks acceptable. The ms are estimates for a picture that changed
completely (waveform + about 55 ms data at 4 MHz SPI + waiting for the next emulator frame); the log line has the
measured split.

| Preset | Waveform | Frames | Hz | Power between refreshes | Full every | Expected ms per picture |
|---|---|---|---|---|---|---|
| 0 (default) | GxEPD2 partial 30/5/30/5 | 70 | 50 | off (PON and POF every time) | 300 (`menuconfig`) | 1680 (measured) |
| 1 | same | 70 | 50 | held | 600 | 1470 |
| 2 | one phase, 1 bit | 20 | 50 | held | 600 | 470 |
| 3 | one phase, 1 bit | 10 | 50 | held | 600 | 270 |
| 4 | one phase, 1 bit | 6 | 100 | held | 600 | 130 |
| 5 | one phase, 1 bit | 4 | 200 | held | 600 | 90 |

Held power goes off after 2 s without a refresh and before every full refresh. Preset 3 looks good on the
owner's OG; the automatic full refresh is rare because it flashes (Y cleans up on demand). The one-phase waveform is not charge balanced per refresh and the frame rates above 50 Hz are from
the UC8179 datasheet table. If a preset leaves the picture wrong, go back with LB; the next full refresh cleans up.

| Where | What |
|---|---|
| `rom` 0x190000, 1 MB | the .gb file as is (Pokemon Red: exactly 1 MB, fits) |
| `save` 0x290000, 64 KB | cart RAM with a CRC. Written when the game saved and the RAM was quiet for 2 s, at most once per 30 s, and only the 4 KB sectors whose content changed. A cut during a write is detected by the CRC and the save then reads as empty. Wait for the log line `gb save=written sectors=N` (or 32 s) before unplugging |
| `nvs` 0x2A0000, 24 KB | Bluetooth bond |
| `state` 0x2B0000, 104 KB | save states: two alternating 52 KB slots, each the whole emulator plus cart RAM with a CRC and the game's identity. Written every 2 minutes (only changed sectors are rewritten; the game pauses well under a second; log `gb state=written`). At boot the newest valid slot of the loaded game is resumed (`gb state=resumed`), so a power cut loses at most 2 minutes. A slot of another game, or a damaged one, is refused and the game starts normally |

| Page section `Save states` | Does |
|---|---|
| Download | writes the `save` and `state` regions to `trmnl-og-gameboy-saves-<date>.bin` (the device restarts) |
| Upload | puts such a file back (checked for size and layout) |
| Clear | after a confirm, erases both: the game starts fresh |

The first firmware flash that carries this partition table adds `state`; ROM, save and Bluetooth bond keep their offsets and stay.

The save belongs to the game that wrote it: before another game with battery RAM, erase it
(`esptool.py --chip esp32c3 erase_region 0x290000 0x10000`). To pair another controller erase `nvs`
(`erase_region 0x2A0000 0x6000`).

## Build

| Step | Command |
|---|---|
| ESP-IDF cache (once, ~3 GB, shared with tiny-paper) | `make setup` |
| Firmware | `make build` -> `build/dist/gameboy-merged.bin` (write at 0x0), ~570 KB app in a 1.5 MB slot |
| Threshold instead of dither | `make build GB_SHADE_MODE=threshold` (two dark shades black, two light white). Compare: `docs/acid2-120.png`, `docs/acid2-threshold-120.png` |
| Host check (gcc only) | `make host-check` -> PNGs in `docs/`; `make -C tools/host test` (save policy, Xbox report parser) |
| QEMU boot (no panel, no Bluetooth) | `tools/idf/idf.sh tools/qemu.sh tests/roms/libbet.gb 40` |
| Panel profile | GEN2 with GxEPD2 partial LUTs (tiny-paper's tuned OG settings); `GB_EPD_T7` in `idf.py menuconfig` for GDEY075T7 |
| Pages | `.github/workflows/pages.yml` builds on every push to main and publishes `tools/web` with the image |

## Flash and load without the hosted page

| Step | How |
|---|---|
| Local page | `make build`, then `cd tools/web && python3 -m http.server`, open http://localhost:8000 (Chrome or Edge). `Flash firmware` takes the fresh build, or pick a file |
| Firmware by hand | `python -m esptool --chip esp32c3 write_flash 0x0 build/dist/gameboy-merged.bin` |
| ROM by hand | `tools/load_rom.sh game.gb [/dev/ttyACM0]`, which is `esptool.py --chip esp32c3 write_flash 0x190000 game.gb`. Test ROMs: `tools/fetch_roms.sh` |
| Log (USB serial) | each second `gb fps_emu=.. fps_panel=.. partial_ms=.. preset=.. frames=.. hz=.. data_ms=.. refresh_ms=.. pon_ms=.. pof_ms=.. full_every=.. heap_free=.. heap_min=..` (last partial refresh: `data_ms` SPI, `refresh_ms` waveform, `pon_ms`/`pof_ms` power on/off, 0 = skipped); `gb speed=N ..` on a preset change; `pad scanning`, `pad link`, `pad connected`, `pad report buttons=0x..`, `pad disconnected`; `gb save=loaded/empty/written` |

Without a valid ROM the firmware prints an error every 5 s. Flashing 0x0 does not touch ROM, save or bond.

## Expected

Not measured on hardware yet (that is the point). Emulation: 59.7 fps wanted, the C3 may reach less. Panel: a partial refresh
of the OTP waveform takes about 1 to 1.7 s, so expect about 1 picture per second, a full refresh (flashes, several
seconds) every 20. RAM: 81 KB static, about 150 KB heap at start; after cart RAM (32 KB), tasks and Bluetooth expect
50 to 60 KB free (`heap_free` in the log tells). Bluetooth pairing and the report format are untested without the
hardware; the parser test uses reports built from the published HID descriptor. Test ROMs: `tests/ROMS.md`.
