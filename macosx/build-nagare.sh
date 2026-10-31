#!/usr/bin/env bash
# Builds Nagare for this Mac and installs it in /Applications.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build="${NAGARE_BUILD_DIR:-$root/../nagare-build/dd}"
git -C "$root" submodule update --init --recursive --depth 1 --quiet
xcodebuild -project "$root/Transmission.xcodeproj" -scheme Transmission -configuration Release -derivedDataPath "$build" \
    ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO DEVELOPMENT_TEAM= build | tail -1
ditto "$build/Build/Products/Release/Nagare.app" /Applications/Nagare.app
xattr -cr /Applications/Nagare.app
codesign -s - -f --deep /Applications/Nagare.app
echo "Installed /Applications/Nagare.app"
