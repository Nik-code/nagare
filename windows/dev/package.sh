#!/usr/bin/env bash
# Builds on the Windows PC, packages there, and brings the zip and installer back to windows/dist.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
slot="${NAGARE_SLOT:-main}"
remote="C:\\dev\\slots\\$slot"
host="${NAGARE_WINDOWS_HOST:-windows-pc}"
quiet='post-quantum|store now|may need to be upgraded'

"$root/dev/build.sh" nagare
COPYFILE_DISABLE=1 tar -C "$root/.." -cf - COPYING licenses | ssh -o BatchMode=yes "$host" "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force $remote\\legal | Out-Null; tar -xf - -C $remote\\legal\"" 2>&1 | grep -v -E "$quiet" || true
ssh -o BatchMode=yes "$host" "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\src\\dev\\package.ps1 -Source $remote\\src -Build $remote\\build -Legal $remote\\legal -Out $remote\\dist" 2>&1 | grep -v -E "$quiet"
mkdir -p "$root/dist"
scp -q "$host:C:/dev/slots/$slot/dist/*" "$root/dist/" 2>&1 | grep -v -E "$quiet" || true
ls -l "$root/dist"
