#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
linux_build=${VKM_LINUX_BUILD_DIR:-"$project_dir/build-linux"}
qt_prefix=${QT_ROOT_DIR:-${Qt6_DIR:-}}
if [ -z "$qt_prefix" ]; then
    printf 'Set QT_ROOT_DIR to the installed Linux Qt 6.11.1 SDK.\n' >&2
    exit 2
fi
test "$(uname -s)" = Linux
test "$(uname -m)" = x86_64

cmake -S "$project_dir" -B "$linux_build" -G Ninja \
    -DCMAKE_CXX_COMPILER_LAUNCHER="${VKM_COMPILER_LAUNCHER:-}" \
    -DCMAKE_C_COMPILER_LAUNCHER="${VKM_COMPILER_LAUNCHER:-}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt_prefix" \
    -DCMAKE_INSTALL_PREFIX=/opt/vkm -DCMAKE_INSTALL_LIBDIR=lib \
    -DVKM_ENABLE_API_INSPECTOR=OFF -DBUILD_TESTING=ON
cmake --build "$linux_build" --parallel "${VKM_BUILD_JOBS:-2}"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    ctest --test-dir "$linux_build" --output-on-failure --parallel 2

mkdir -p "$project_dir/dist/ubuntu"
(cd "$linux_build" && cpack -G DEB -B "$project_dir/dist/ubuntu")
deb_file=$(find "$project_dir/dist/ubuntu" -maxdepth 1 -name 'vkmessenger_*_ubuntu24.04_amd64.deb' -print -quit)
test -n "$deb_file"
package_stage=$(mktemp -d)
trap 'rm -rf "$package_stage"' EXIT HUP INT TERM
dpkg-deb -x "$deb_file" "$package_stage"
package_executable="$package_stage/opt/vkm/bin/VK"
test -x "$package_executable"
test -f "$package_stage/usr/share/applications/vkmessenger.desktop"
test -f "$package_stage/usr/share/icons/hicolor/1024x1024/apps/vkmessenger.png"

# The installed binary must resolve bundled Qt without SDK search paths.
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$package_executable" --self-check
env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
    QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software xvfb-run -a "$package_executable" --self-check
desktop-file-validate "$package_stage/usr/share/applications/vkmessenger.desktop"
(
    cd "$project_dir/dist/ubuntu"
    for file in *.deb; do sha256sum "$file" > "$file.sha256"; done
)
printf 'Ubuntu package verified: %s\n' "$deb_file"
