#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
icon_source="$project_dir/resources/icons/VK.png"
icon_output="$project_dir/resources/icons/VK.icns"
icon_work=$(mktemp -d "${TMPDIR:-/tmp}/vkm-icon.XXXXXX")
trap 'rm -rf "$icon_work"' EXIT HUP INT TERM
icon_set="$icon_work/VK.iconset"
mkdir -p "$icon_set"

for icon_size in 16 32 128 256 512; do
    sips -z "$icon_size" "$icon_size" "$icon_source" --out "$icon_set/icon_${icon_size}x${icon_size}.png" >/dev/null
    retina_size=$((icon_size * 2))
    sips -z "$retina_size" "$retina_size" "$icon_source" --out "$icon_set/icon_${icon_size}x${icon_size}@2x.png" >/dev/null
done

iconutil -c icns "$icon_set" -o "$icon_output"
printf 'Created %s\n' "$icon_output"
