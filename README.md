# trmnl-og-gameboy

A separate firmware for the TRMNL OG (ESP32-C3, 800x480 e-paper): a Game Boy emulator on e-ink. Flash it like any
image; flash tiny-paper again to go back. Proof of concept: playability at e-ink frame rates is the open question.

| | |
|---|---|
| Emulator | Peanut-GB (MIT), one C file |
| Panel | the OG driver from tiny-paper (GPL-3.0) |
| Games | homebrew or games you own; loaded over USB |
| Controller | later: Bluetooth LE gamepad (the C3 has BLE only) |

License: GPL-3.0-or-later.
