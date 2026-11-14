#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'Usage: sh scripts/package-macos-release.sh dist/VK-version-macOS-arm64.zip\n' >&2
    exit 2
fi
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
app_bundle="$project_dir/build/VK.app"
case "$1" in
    /*) release_output=$1 ;;
    *) release_output="$project_dir/$1" ;;
esac
release_work=$(mktemp -d "${TMPDIR:-/tmp}/vkm-release.XXXXXX")
trap 'rm -rf "$release_work"' EXIT HUP INT TERM

# A graphical macOS session is required. Offscreen testing does not exercise
# Cocoa startup or the native Dock icon, which are essential release checks.
native_check() {
    env -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
        QT_QPA_PLATFORM=cocoa "$1/Contents/MacOS/VK" --self-check
}
codesign --verify --deep --strict "$app_bundle"
native_check "$app_bundle"
ditto -c -k --sequesterRsrc --keepParent "$app_bundle" "$release_work/release.zip"
ditto -x -k "$release_work/release.zip" "$release_work/unpacked"
codesign --verify --deep --strict "$release_work/unpacked/VK.app"
native_check "$release_work/unpacked/VK.app"
unzip -tq "$release_work/release.zip"

mkdir -p "$(dirname -- "$release_output")"
mv "$release_work/release.zip" "$release_output"
(
    cd -- "$(dirname -- "$release_output")"
    release_name=$(basename -- "$release_output")
    shasum -a 256 "$release_name" > "$release_name.sha256"
)
printf 'Verified release: %s\n' "$release_output"
