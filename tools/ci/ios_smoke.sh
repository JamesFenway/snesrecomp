#!/usr/bin/env bash
# Build a ROM-free project from tools/new_project/templates as a SETUP HOST,
# to prove the runner and desktop host compile and link for a platform.
# No ROM and no generated code are involved, so this runs anywhere.
#
#   bash tools/ci/ios_smoke.sh ios   [work-dir]   # macOS + Xcode: iOS app, unsigned
#   bash tools/ci/ios_smoke.sh host  [work-dir]   # this machine's desktop build
#
# The `host` mode is how the script itself is checked off a Mac.
set -euo pipefail

MODE=${1:-ios}
FRAMEWORK=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
WORK=${2:-"$FRAMEWORK/build-ios-smoke"}
PROJECT=SnesrecompIosSmoke
TEMPLATES="$FRAMEWORK/tools/new_project/templates"
PYTHON=${PYTHON:-python3}

rm -rf "$WORK"
mkdir -p "$WORK/src/gen" "$WORK/recomp" "$WORK/mods/preloaded/packages"
ln -s "$FRAMEWORK" "$WORK/snesrecomp"

fill() {
    "$PYTHON" "$FRAMEWORK/tools/new_project/fill_tokens.py" \
        "$TEMPLATES/$1" "$WORK/$2" \
        --set "PROJECT_NAME=$PROJECT" \
        --set "DISPLAY_NAME=iOS smoke" \
        --set "WINDOW_TITLE=iOS smoke" \
        --set "CATALOG_SLUG=ios-smoke" \
        --set "ROM_FILE=ios_smoke.sfc" \
        --set "ROM_SLUG=ios-smoke" \
        --set "ROM_MAPPING=lorom" \
        --set "ROM_CRC32=00000000" \
        --set "ROM_SHA256=0000000000000000000000000000000000000000000000000000000000000000" \
        --set "ROM_MD5=00000000000000000000000000000000" \
        --set "ROM_SHA1=0000000000000000000000000000000000000000" \
        --set "ROM_SIZE=1048576" \
        --set "REGION=USA" \
        --set "REGION_NAME=USA" \
        --set "GAME_ID=ios-smoke" \
        --set "COPROCESSOR=none" \
        --set "ZIP_PREFIX=ios-smoke" \
        --set "PLAYERS=2" \
        --set "PLAYERS_NOTE=" \
        --set "NETPLAY_BLOCK=# Netplay is not built into this target." \
        --set "LAUNCHER_BLOCK=# No GUI launcher." \
        --set "MULTITAP_BLOCK=# No multitap." \
        --set "SETUP_DATE=2026-10-25" \
        --set "DESCRIPTION=" \
        --set "DESCRIPTION_SOURCE=" \
        --set "PUBLISHER=-" \
        --set "DEVELOPER=-" \
        --set "YEAR=-" \
        --set "GITHUB_OWNER=-" \
        --set "GITHUB_REPO=-"
}

fill CMakeLists.txt.in   CMakeLists.txt
fill VERSION.in          VERSION
fill main.c.in           src/main.c
fill game_rtl.c.in       src/game_rtl.c
fill game_rtl.h.in       src/game_rtl.h
fill gen_stubs.c.in      src/gen_stubs.c
fill variables.h.in      src/variables.h
fill host_contract.c.in  src/host_contract.c
fill rom_identity.txt.in rom_identity.txt
# A config.ini seed, so the payload/ staging path is exercised too.
printf '[General]\nAutosave = 1\n' > "$WORK/config.ini"
printf '\nsnesrecomp_target_config_seed(%s)\n' "$PROJECT" >> "$WORK/CMakeLists.txt"

case "$MODE" in
ios)
    cmake -S "$WORK" -B "$WORK/build" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DSNESRECOMP_SETUP_HOST=ON \
        ${SNESRECOMP_SDL3_SOURCE_DIR:+-DSNESRECOMP_SDL3_SOURCE_DIR="$SNESRECOMP_SDL3_SOURCE_DIR"}
    cmake --build "$WORK/build" --config Release --target "$PROJECT" -- \
        -sdk iphoneos -quiet CODE_SIGNING_ALLOWED=NO
    APP=$(find "$WORK/build" -name "$PROJECT.app" -type d | head -n 1)
    test -n "$APP"
    test -x "$APP/$PROJECT"
    test -f "$APP/Info.plist"
    plutil -lint "$APP/Info.plist"
    echo "ios_smoke: built $APP"
    ls -la "$APP"
    test -f "$APP/payload/config.ini"
    ls -laR "$APP/payload"
    ;;
host)
    cmake -S "$WORK" -B "$WORK/build" -DCMAKE_BUILD_TYPE=Release \
        -DSNESRECOMP_SETUP_HOST=ON \
        ${SNESRECOMP_SDL3_SOURCE_DIR:+-DSNESRECOMP_SDL3_SOURCE_DIR="$SNESRECOMP_SDL3_SOURCE_DIR"} \
        ${SNESRECOMP_SMOKE_CMAKE_ARGS:-}
    cmake --build "$WORK/build" --target "$PROJECT" -j "$(nproc 2>/dev/null || echo 2)"
    echo "ios_smoke: built the host variant in $WORK/build"
    ;;
*)
    echo "usage: $0 ios|host [work-dir]" >&2
    exit 2
    ;;
esac
