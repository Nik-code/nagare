#!/usr/bin/env bash
# Sends the engine files Nagare patches to the Windows PC and rebuilds the engine there. Its source tree is a
# clone of the published branch, so local engine changes have to travel this way until they're pushed.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
host="${NAGARE_WINDOWS_HOST:-windows-pc}"
quiet='post-quantum|store now|may need to be upgraded'
for name in peer-mgr.cc peer-mgr-wishlist.cc; do
    scp -q "$root/libtransmission/$name" "$host:C:/dev/nagare-src/libtransmission/$name" 2>&1 | grep -v -E "$quiet" || true
done
ssh -o BatchMode=yes "$host" "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\dev\\slots\\${NAGARE_SLOT:-main}\\src\\dev\\remote-engine-rebuild.ps1" 2>&1 | grep -v -E "$quiet"
