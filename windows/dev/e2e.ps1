# Runs on the Windows PC: the real app and engine against the live Sintel swarm, in a throwaway profile.
param([string]$Phase = 'download', [string]$Name = 'real', [string]$Extra = '', [string]$Slot = 'main')
$root = 'C:\dev\e2e'
$exe = "C:\dev\slots\$Slot\build\nagare.exe"
$runner = "C:\dev\slots\$Slot\src\dev\run-interactive.ps1"
$measure = "C:\dev\slots\$Slot\src\dev\measure.ps1"
$magnet = 'magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Sintel&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A6969'

if ($Phase -eq 'download') {
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$root\cfg", "$root\downloads" | Out-Null
    '{ "AskedToBeDefault": true, "MagnetOpenAsk": false, "DownloadAsk": false, "QBittorrentImported": true, "CheckQuit": false, "ThemeMode": 2 }' | Set-Content "$root\cfg\prefs.json" -Encoding ASCII
    '{ "download-dir": "C:\\dev\\e2e\\downloads", "incomplete-dir-enabled": false }' | Set-Content "$root\cfg\settings.json" -Encoding ASCII
    & $runner -Exe $exe -Arguments "--config $root\cfg `"$magnet`"" -WaitSeconds 25 -Task NagareE2E
    'downloading:'
    & $measure -Seconds 10
    $deadline = (Get-Date).AddSeconds(300)
    do {
        Start-Sleep -Seconds 5
        $size = (Get-ChildItem "$root\downloads" -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -ne '.part' } | Measure-Object Length -Sum).Sum
        $parts = (Get-ChildItem "$root\downloads" -Recurse -Filter *.part -ErrorAction SilentlyContinue | Measure-Object).Count
    } while (($parts -gt 0 -or $size -lt 100MB) -and (Get-Date) -lt $deadline)
    "files: $size bytes, $parts partial"
    Start-Sleep -Seconds 15
    'seeding:'
    & $measure -Seconds 12
    Get-ChildItem "$root\downloads" -Recurse -File | Select-Object -First 14 | ForEach-Object { "$($_.Length) $($_.Name)" }
}
if ($Phase -eq 'shot') {
    & $runner -Exe $exe -Arguments "--config $root\cfg --shot $root\$Name.png $Extra" -WaitSeconds 7 -Task NagareE2E
}
if ($Phase -eq 'quit') {
    $before = Get-Date
    # Closing has to come from the signed-in desktop, since an SSH session can't reach the window.
    & $runner -Exe 'powershell.exe' -Arguments "-NoProfile -Command (Get-Process nagare).CloseMainWindow()" -WaitSeconds 0 -Task NagareE2EQuit | Out-Null
    do { Start-Sleep -Milliseconds 250 } while ((Get-Process nagare -ErrorAction SilentlyContinue) -and ((Get-Date) - $before).TotalSeconds -lt 15)
    "quit took $([int]((Get-Date) - $before).TotalMilliseconds) ms, running: $([bool](Get-Process nagare -ErrorAction SilentlyContinue))"
    Get-ChildItem "$root\cfg" -Recurse -File | Select-Object -First 12 | ForEach-Object { "$($_.Length) $($_.FullName)" }
}
