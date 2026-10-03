# trmnl-og-gameboy (agent notes)
- Separate firmware for the TRMNL OG (ESP32-C3, 4 MB flash, no PSRAM, 800x480 1-bit e-paper, one button). ESP-IDF 5.5.1.
- Reuse from ~/workspace/tiny-paper (GPL-3.0, keep SPDX lines and attribution): the OG panel driver (firmware/components/epd, hal_esp, firmware/boards/trmnl_og), the docker build image tiny-paper-idf:v5.5.1 and its make esp wrapper pattern.
- Never ship ROMs: only homebrew test ROMs with a free license, downloaded at build/test time, gitignored.
- No em-dashes. Docs brief, tables over prose. Small commits on main.
