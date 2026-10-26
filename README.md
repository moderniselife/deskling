# Deskling firmware v0.3.1

Prebuilt firmware for the Deskling screen. Don't edit this branch by hand: `firmware/tools/release.sh` replaces it.

- Install from the browser: https://deskling.mojolayers.com/install (or `/flash` on your relay).
- `deskling-<version>-<variant>.bin` is the app image (for over-the-air updates and the screen's settings page).
- `deskling-<version>-<variant>-full.bin` is the whole flash from address 0 (for esptool or the browser installer).
- Screens check https://deskling.mojolayers.com/firmware/manifest.json, which serves this branch.
