#!/usr/bin/env bash
# Runs the built app on the Windows PC's desktop, waits for it to capture itself and fetches the PNG.
# Usage: dev/shot.sh <local-output.png> [app arguments...]
set -euo pipefail
out="$1"; shift
slot="${NAGARE_SLOT:-main}"
host="${NAGARE_WINDOWS_HOST:-windows-pc}"
quiet='post-quantum|store now|may need to be upgraded'
remote="C:\\dev\\slots\\$slot\\shot.png"
args="--shot $remote $*"
ssh -o BatchMode=yes "$host" "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Remove-Item $remote -ErrorAction SilentlyContinue; & C:\\dev\\slots\\$slot\\src\\dev\\run-interactive.ps1 -Exe C:\\dev\\slots\\$slot\\build\\nagare.exe -Arguments '$args' -WaitSeconds 6 -Task Nagare$slot\"" 2>&1 | grep -v -E "$quiet"
scp -q "$host:C:/dev/slots/$slot/shot.png" "$out" 2>&1 | grep -v -E "$quiet" || true
ls -l "$out"
