#!/usr/bin/env bash
# Builds a release of Nagare and packs it as a disk image. Usage: package-nagare.sh <version> [output dir]
#
# Signed ad hoc unless NAGARE_SIGN_IDENTITY is set to a Developer ID certificate. To notarise as well,
# set NAGARE_NOTARY_PROFILE to a profile saved with `xcrun notarytool store-credentials`.
set -euo pipefail
version=${1:?usage: package-nagare.sh <version> [output dir]}
root=$(cd "$(dirname "$0")/.." && pwd)
out=${2:-$root/../nagare-build/release}
build="$out/derived"
identity=${NAGARE_SIGN_IDENTITY:--}
mkdir -p "$out"

git -C "$root" submodule update --init --recursive --depth 1 --quiet

# The prefix map keeps this machine's paths out of the binaries.
map="-ffile-prefix-map=$root=."
xcodebuild -project "$root/Transmission.xcodeproj" -scheme Transmission -configuration Release -derivedDataPath "$build" \
    ARCHS=arm64 ONLY_ACTIVE_ARCH=YES CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO DEVELOPMENT_TEAM= \
    OTHER_CFLAGS="\$(inherited) $map" OTHER_CPLUSPLUSFLAGS="\$(inherited) $map" build | tail -1

stage="$out/stage"
rm -rf "$stage"
mkdir -p "$stage"
ditto "$build/Build/Products/Release/Nagare.app" "$stage/Nagare.app"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$stage/Nagare.app/Contents/Info.plist"
xattr -cr "$stage/Nagare.app"

if rg -q --binary "$HOME" "$stage/Nagare.app"; then
    echo "error: the build still contains $HOME" >&2
    exit 1
fi

if [ "$identity" = "-" ]; then
    codesign -s - -f --deep "$stage/Nagare.app"
else
    codesign -s "$identity" -f --deep --options runtime --timestamp "$stage/Nagare.app"
fi

ln -s /Applications "$stage/Applications"
dmg="$out/Nagare-$version.dmg"
rm -f "$dmg"
hdiutil create -quiet -volname "Nagare $version" -srcfolder "$stage" -format ULMO -ov "$dmg"

if [ -n "${NAGARE_NOTARY_PROFILE:-}" ]; then
    codesign -s "$identity" --timestamp "$dmg"
    xcrun notarytool submit "$dmg" --keychain-profile "$NAGARE_NOTARY_PROFILE" --wait
    xcrun stapler staple "$dmg"
fi

shasum -a 256 "$dmg"
