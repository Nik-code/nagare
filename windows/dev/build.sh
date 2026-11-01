#!/usr/bin/env bash
# Sends windows/ to the Windows PC and builds it there. NAGARE_SLOT keeps parallel worktrees apart.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-all}"
slot="${NAGARE_SLOT:-main}"
host="${NAGARE_WINDOWS_HOST:-windows-pc}"
quiet='post-quantum|store now|may need to be upgraded'
COPYFILE_DISABLE=1 tar -C "$root" -cf - --exclude=.DS_Store --exclude='._*' --exclude=.shots . | ssh -o BatchMode=yes "$host" "powershell -NoProfile -Command \"Remove-Item -Recurse -Force C:\\dev\\slots\\$slot\\src -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\\dev\\slots\\$slot\\src | Out-Null; tar -xf - -C C:\\dev\\slots\\$slot\\src\"" 2>&1 | grep -v -E "$quiet" || true
ssh -o BatchMode=yes "$host" "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\dev\\slots\\$slot\\src\\dev\\remote-build.ps1 -Source C:\\dev\\slots\\$slot\\src -Build C:\\dev\\slots\\$slot\\build -Target $target" 2>&1 | grep -v -E "$quiet"
