#!/usr/bin/env bash
# Regenerates the app icon from transmission-new.svg.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
set_dir="$here/../Images.xcassets/AppIcon.appiconset"
for size in 16 32 128 256 512; do
    rsvg-convert -w "$size" -h "$size" "$here/transmission-new.svg" -o "$set_dir/icon_${size}x${size}.png"
    rsvg-convert -w $((size * 2)) -h $((size * 2)) "$here/transmission-new.svg" -o "$set_dir/icon_${size}x${size}@2x.png"
done
