# Hardware notes

## Board

- **Model:** Guition JC8012P4A1C_I_W_Y (sticker SKU 10153001). Made by Shenzhen Jingcai Intelligent.
- **MCU:** ESP32-P4 with 32 MB PSRAM.
- **Radio:** ESP32-C6 companion chip, Wi-Fi 6 + BLE 5, talks to the P4 over ESP-Hosted. The P4 itself has no radio.
- **Display:** 10.1" IPS, 800×1280, MIPI-DSI, capacitive touch (GSL3680 controller).
- **Vendor examples:** https://github.com/guitionofficial (ESP-IDF and Arduino demos).
- **Vendor support:** technical questions go to mark@guition.com.

## Known gotchas

- **Power:** needs a stable 5 V / 1 A or better. The vendor warns against powering it from a computer.
- **C6 firmware:** some boards ship with ESP32-C6 firmware too old for the current `esp_hosted` component. Reflash the C6 before chasing Wi-Fi bugs.
- **Touch init:** some vendor demos fail to initialise the GSL3680 (`i2c transaction failed` / `gsl3680 read error`). Confirm touch works on a known-good example before building UI.
- **Toolchain:** ESP-IDF 5.5.x plus LVGL 9 is the target. Arduino support for the P4 is still immature.
- **Silicon:** P4 revision 3.x boards changed power rails, so early and late boards may differ.

## Housing measurements needed

No published dimension drawing was found for this board. Measure the bare module out of its box, with calipers if possible:

- [ ] overall width × height × depth (panel + board)
- [ ] mounting hole positions and diameter
- [ ] USB-C port position and height off the back
- [ ] any other connectors (UART, speaker, battery, camera)
- [ ] how far the glass extends past the metal frame, and the bezel width around the active area
- [ ] a straight-on photo of the back next to a ruler
