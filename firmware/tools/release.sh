#!/usr/bin/env bash
# Build every board variant locally and publish them to the `firmware-dist` branch.
# No CI needed: the browser installer (/flash on your relay) and the screen's
# over-the-air updater both read from that branch on raw.githubusercontent.com.
#
#   . $IDF_PATH/export.sh            # ESP-IDF 6.x in your shell
#   firmware/tools/release.sh v0.2.0 # build, pack, push
#   firmware/tools/release.sh v0.2.0 --no-push   # build and pack only (see firmware/dist/)
#   firmware/tools/release.sh v0.2.0 --publish-only   # push what's already in firmware/dist/
#
# Variants: chip revision (p4r1 = silicon v1.x, p4r3 = v3.x) × LCD panel (lcd1 / lcd2).
set -euo pipefail

VERSION="${1:-}"
PUSH=1
BUILD=1
case "${2:-}" in
  --no-push) PUSH=0 ;;
  --publish-only) BUILD=0 ;;
esac
if [[ -z "$VERSION" ]]; then
  echo "usage: $0 <version, e.g. v0.2.0> [--no-push | --publish-only]" >&2
  exit 1
fi

FW="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$FW/.." && pwd)"
DIST="$FW/dist"
VARIANTS=(p4r1-lcd1 p4r1-lcd2 p4r3-lcd1 p4r3-lcd2)

if [[ $BUILD == 1 ]]; then
command -v idf.py >/dev/null || { echo "ESP-IDF isn't in this shell. Run: . \$IDF_PATH/export.sh" >&2; exit 1; }
rm -rf "$DIST"
mkdir -p "$DIST"
cd "$FW"

for v in "${VARIANTS[@]}"; do
  chip="${v%-*}"
  lcd="${v#*-}"
  b="build-$v"
  echo "==> $v"
  # A fresh sdkconfig per variant, built only from the checked-in defaults (never your local Wi-Fi/relay overrides).
  rm -f "$b/sdkconfig"
  DESKLING_VERSION="$VERSION" idf.py -B "$b" \
    -DSDKCONFIG="$b/sdkconfig" \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;config/$chip.defaults;config/$lcd.defaults" \
    reconfigure build >"$b.log" 2>&1 || { tail -40 "$b.log"; echo "build $v failed (full log: firmware/$b.log)" >&2; exit 1; }

  # File names carry the version, so a published build never changes (and can be cached forever).
  cp "$b/deskling.bin" "$DIST/deskling-$VERSION-$v.bin"
  # One image from address 0 for the browser installer: bootloader + partition table + OTA data + app.
  (cd "$b" && python -m esptool --chip esp32p4 merge-bin -o "$DIST/deskling-$VERSION-$v-full.bin" @flash_args >/dev/null)
  printf '    app %s KB, full image %s KB\n' \
    $(( $(wc -c <"$DIST/deskling-$VERSION-$v.bin") / 1024 )) $(( $(wc -c <"$DIST/deskling-$VERSION-$v-full.bin") / 1024 ))
done

python3 - "$DIST" "$VERSION" "${VARIANTS[@]}" <<'EOF'
import hashlib, json, sys, datetime, os
dist, version, variants = sys.argv[1], sys.argv[2], sys.argv[3:]
def sha(p): return hashlib.sha256(open(os.path.join(dist, p), "rb").read()).hexdigest()
builds = {}
for v in variants:
    app, full = f"deskling-{version}-{v}.bin", f"deskling-{version}-{v}-full.bin"
    builds[v] = {"app": app, "full": full, "app_sha256": sha(app), "full_sha256": sha(full)}
manifest = {"version": version, "date": datetime.date.today().isoformat(), "builds": builds}
json.dump(manifest, open(os.path.join(dist, "manifest.json"), "w"), indent=2)
EOF

cat >"$DIST/README.md" <<EOF
# Deskling firmware $VERSION

Prebuilt firmware for the Deskling screen. Don't edit this branch by hand: \`firmware/tools/release.sh\` replaces it.

- Install from the browser: https://deskling.mojolayers.com/install (or \`/flash\` on your relay).
- \`deskling-<version>-<variant>.bin\` is the app image (for over-the-air updates and the screen's settings page).
- \`deskling-<version>-<variant>-full.bin\` is the whole flash from address 0 (for esptool or the browser installer).
- Screens check https://deskling.mojolayers.com/firmware/manifest.json, which serves this branch.
EOF

echo "==> packed $VERSION in firmware/dist"
fi

if [[ $BUILD == 0 ]]; then
  grep -q "\"version\": \"$VERSION\"" "$DIST/manifest.json" 2>/dev/null \
    || { echo "firmware/dist doesn't hold $VERSION. Build it first." >&2; exit 1; }
fi
[[ $PUSH == 1 ]] || exit 0

# Publish: the branch only ever holds the latest release, as a single orphan commit.
REMOTE="$(git -C "$REPO" remote get-url origin)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp -R "$DIST/." "$TMP/"
git -C "$TMP" init -q -b firmware-dist
git -C "$TMP" add -A
git -C "$TMP" -c user.name="$(git -C "$REPO" config user.name || echo deskling)" \
  -c user.email="$(git -C "$REPO" config user.email || echo deskling@localhost)" \
  commit -q -m "Firmware $VERSION"
git -C "$TMP" push -q -f "$REMOTE" firmware-dist
echo "==> published $VERSION to the firmware-dist branch"
