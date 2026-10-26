# Working on Deskling

## Commits

- Do not add `Co-Authored-By` trailers (or any other AI attribution line) to commit messages.
- Commits are authored by the repo owner: `git config user.name "Joseph Shenton"` and `git config user.email "joe@mojos.email"`. Never commit as Claude.
- Keep commit messages short and descriptive.

## Project shape

- `firmware/` is ESP-IDF + LVGL for the Guition JC8012P4A1C (ESP32-P4, 10.1" 800×1280 panel used rotated to 1280×800 landscape).
- `relay/` is a Cloudflare Worker.
- `hooks/` must work on both macOS and Windows.
- The UI design and motion rules are in `docs/design.md`. Keep the firmware UI faithful to them.
