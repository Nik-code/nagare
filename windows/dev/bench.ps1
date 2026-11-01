# Runs on the Windows PC: Nagare against qBittorrent on the same machine, network and torrent, one at a time.
# Each client gets a fresh throwaway profile, so neither touches anything in daily use.
param([int]$Rounds = 3, [string]$Slot = 'work', [string]$Out = 'C:\dev\bench\results.csv')

$root = 'C:\dev\bench'
$nagare = "C:\dev\slots\$Slot\build\nagare.exe"
$qbit = "$env:ProgramFiles\qBittorrent\qbittorrent.exe"
$runner = "C:\dev\slots\$Slot\src\dev\run-interactive.ps1"
$magnet = 'magnet:?xt=urn:btih:08ada5a7a6183aae1e09d831df6748d566095a10&dn=Sintel&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce&tr=udp%3A%2F%2Fexodus.desync.com%3A6969%2Fannounce'
$expectedBytes = 129302391
$settleSeconds = 20
$sampleSeconds = 30
$timeoutSeconds = 420

function Reset-Profile([string]$name) {
    $folder = "$root\$name"
    Remove-Item -Recurse -Force $folder -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force "$folder\cfg", "$folder\downloads" | Out-Null
    return $folder
}

function Prepare-Nagare([string]$folder) {
    '{ "AskedToBeDefault": true, "MagnetOpenAsk": false, "DownloadAsk": false, "QBittorrentImported": true, "CheckQuit": false, "ThemeMode": 2 }' | Set-Content "$folder\cfg\prefs.json" -Encoding ASCII
    $downloads = "$folder\downloads".Replace('\', '\\')
    "{ `"download-dir`": `"$downloads`", `"incomplete-dir-enabled`": false, `"peer-port`": 51499, `"rename-partial-files`": true }" | Set-Content "$folder\cfg\settings.json" -Encoding ASCII
}

function Prepare-Qbit([string]$folder) {
    $config = "$folder\cfg\qBittorrent\config"
    New-Item -ItemType Directory -Force $config | Out-Null
    $downloads = "$folder\downloads".Replace('\', '/')
    @"
[LegalNotice]
Accepted=true

[Application]
GUI\Notifications\Enabled=false

[BitTorrent]
Session\DefaultSavePath=$downloads
Session\Port=51498
Session\AddExtensionToIncompleteFiles=true

[Preferences]
General\ExitConfirm=false
General\CloseToTray=false
General\MinimizeToTray=false
WebUI\Enabled=false
"@ | Set-Content "$config\qBittorrent.ini" -Encoding ASCII
}

function Start-Client([string]$exe, [string]$arguments) {
    $name = [IO.Path]::GetFileNameWithoutExtension($exe)
    $already = @(Get-Process $name -ErrorAction SilentlyContinue | ForEach-Object Id)
    & $runner -Exe $exe -Arguments $arguments -WaitSeconds 3 -Task NagareBench | Out-Null
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $process = Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $already -notcontains $_.Id } | Select-Object -First 1
        if ($process) { return $process }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    throw "no new $name process"
}

function Stop-Client($process) {
    & $runner -Exe 'powershell.exe' -Arguments "-NoProfile -Command (Get-Process -Id $($process.Id)).CloseMainWindow()" -WaitSeconds 0 -Task NagareBenchQuit | Out-Null
    $start = Get-Date
    while (-not $process.HasExited -and ((Get-Date) - $start).TotalSeconds -lt 20) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    $seconds = ((Get-Date) - $start).TotalSeconds
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $seconds = -1
    }
    return [math]::Round($seconds, 1)
}

function Read-Memory([int]$id) {
    $perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$id"
    $vram = (Get-Counter "\GPU Process Memory(pid_${id}_*)\Dedicated Usage" -ErrorAction SilentlyContinue).CounterSamples | Measure-Object -Property CookedValue -Sum
    return [pscustomobject]@{ Private = $perf.WorkingSetPrivate / 1MB; Vram = $vram.Sum / 1MB }
}

function Find-Client([string]$exe) {
    # Some clients hand over to a new process on first run, so never trust the one that was launched.
    $name = [IO.Path]::GetFileNameWithoutExtension($exe)
    return Get-Process $name -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Sort-Object StartTime -Descending | Select-Object -First 1
}

function Sample-Quiet($process, [int]$seconds) {
    # The window has to be a fixed length. A process with no GPU work has no counter to wait on, so
    # timing it from Get-Counter would measure almost nothing and report 0% CPU.
    $process.Refresh()
    $cpuStart = $process.TotalProcessorTime.TotalSeconds
    $start = Get-Date
    $gpu = 0.0
    $gpuSamples = 0
    while (((Get-Date) - $start).TotalSeconds -lt $seconds) {
        Start-Sleep -Seconds 1
        $sample = Get-Counter "\GPU Engine(pid_$($process.Id)_*)\Utilization Percentage" -ErrorAction SilentlyContinue
        if ($sample) {
            $gpu += ($sample.CounterSamples | Measure-Object -Property CookedValue -Sum).Sum
        }
        $gpuSamples += 1
    }
    $elapsed = ((Get-Date) - $start).TotalSeconds
    $process.Refresh()
    if ($process.HasExited) { throw 'client exited while being sampled' }
    $cpu = ($process.TotalProcessorTime.TotalSeconds - $cpuStart) / $elapsed / [Environment]::ProcessorCount * 100
    if ($gpuSamples) { $gpu /= $gpuSamples }
    $memory = Read-Memory $process.Id
    return [pscustomobject]@{ Private = $memory.Private; Commit = $process.PrivateMemorySize64 / 1MB; Cpu = $cpu; Gpu = $gpu; Vram = $memory.Vram; Threads = $process.Threads.Count; Handles = $process.HandleCount; Elapsed = $elapsed }
}

function Save-Desktop([string]$path) {
    $script = "$root\capture.ps1"
    @"
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
`$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
`$bitmap = New-Object System.Drawing.Bitmap `$bounds.Width, `$bounds.Height
`$graphics = [System.Drawing.Graphics]::FromImage(`$bitmap)
`$graphics.CopyFromScreen(`$bounds.Location, [System.Drawing.Point]::Empty, `$bounds.Size)
`$bitmap.Save('$path')
"@ | Set-Content $script -Encoding ASCII
    & $runner -Exe 'powershell.exe' -Arguments "-NoProfile -ExecutionPolicy Bypass -File $script" -WaitSeconds 3 -Task NagareBenchShot | Out-Null
}

function Wait-Download($process, [string]$folder) {
    $start = Get-Date
    $cpuStart = $process.TotalProcessorTime.TotalSeconds
    $peak = 0.0
    do {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { throw 'client exited during the download' }
        $peak = [math]::Max($peak, $process.WorkingSet64 / 1MB)
        $files = @(Get-ChildItem "$folder\downloads" -Recurse -File -ErrorAction SilentlyContinue)
        $partial = @($files | Where-Object { $_.Extension -in '.part', '.!qB' }).Count
        $total = ($files | Measure-Object Length -Sum).Sum
        $done = $files.Count -gt 0 -and $partial -eq 0 -and $total -ge $expectedBytes
    } while (-not $done -and ((Get-Date) - $start).TotalSeconds -lt $timeoutSeconds)
    $seconds = ((Get-Date) - $start).TotalSeconds
    $process.Refresh()
    return [pscustomobject]@{ Done = $done; Seconds = $seconds; PeakWs = $peak; CpuSeconds = $process.TotalProcessorTime.TotalSeconds - $cpuStart }
}

function Run-Round([string]$client, [int]$round) {
    $folder = Reset-Profile $client
    if ($client -eq 'nagare') {
        Prepare-Nagare $folder
        $exe = $nagare
        $idleArgs = "--config $folder\cfg"
        $runArgs = "--config $folder\cfg `"$magnet`""
    }
    else {
        Prepare-Qbit $folder
        $exe = $qbit
        $idleArgs = "--profile=$folder\cfg --no-splash"
        $runArgs = "--profile=$folder\cfg --no-splash --skip-dialog=true --save-path=$folder\downloads `"$magnet`""
    }

    # Empty and idle, with the window open.
    $launch = Get-Date
    $process = Start-Client $exe $idleArgs
    $launchSeconds = ((Get-Date) - $launch).TotalSeconds
    Start-Sleep -Seconds $settleSeconds
    $process = Find-Client $exe
    if ($round -eq 1) { Save-Desktop "$root\idle-$client.png" }
    $idle = Sample-Quiet $process $sampleSeconds
    $idleQuit = Stop-Client $process
    Start-Sleep -Seconds 3

    # The same download, then seeding with nothing asked of it.
    $process = Start-Client $exe $runArgs
    $run = Wait-Download $process $folder
    Start-Sleep -Seconds $settleSeconds
    $process = Find-Client $exe
    if ($round -eq 1) { Save-Desktop "$root\seed-$client.png" }
    $seed = Sample-Quiet $process $sampleSeconds

    # The same again with another window in front, which is how a client spends most of its life.
    & $runner -Exe 'powershell.exe' -Arguments "-NoProfile -Command Start-Sleep -Seconds $($sampleSeconds + 6)" -WaitSeconds 3 -Task NagareBenchFront | Out-Null
    $behind = Sample-Quiet $process $sampleSeconds
    Start-Sleep -Seconds 4
    $quit = Stop-Client $process

    $line = [pscustomobject]@{
        client = $client; round = $round; finished = $run.Done
        download_s = [math]::Round($run.Seconds, 1); download_cpu_s = [math]::Round($run.CpuSeconds, 2); peak_ws_mb = [math]::Round($run.PeakWs, 1)
        idle_private_mb = [math]::Round($idle.Private, 1); idle_commit_mb = [math]::Round($idle.Commit, 1); idle_cpu_pct = [math]::Round($idle.Cpu, 3); idle_gpu_pct = [math]::Round($idle.Gpu, 3); idle_vram_mb = [math]::Round($idle.Vram, 1); idle_threads = $idle.Threads; idle_handles = $idle.Handles
        seed_private_mb = [math]::Round($seed.Private, 1); seed_commit_mb = [math]::Round($seed.Commit, 1); seed_cpu_pct = [math]::Round($seed.Cpu, 3); seed_gpu_pct = [math]::Round($seed.Gpu, 3); seed_vram_mb = [math]::Round($seed.Vram, 1)
        behind_cpu_pct = [math]::Round($behind.Cpu, 3); behind_gpu_pct = [math]::Round($behind.Gpu, 3)
        sample_s = [math]::Round($seed.Elapsed, 1); quit_s = $quit
    }
    $line | Export-Csv -Path $Out -Append -NoTypeInformation
    "$client round $round`: finished=$($line.finished) in $($line.download_s)s, idle $($line.idle_private_mb) MB, seeding $($line.seed_private_mb) MB, quit $quit s"
}

New-Item -ItemType Directory -Force $root | Out-Null
Remove-Item $Out -ErrorAction SilentlyContinue
"nagare exe: $((Get-Item $nagare).Length) bytes, qbittorrent $((Get-Item $qbit).VersionInfo.ProductVersion): $((Get-Item $qbit).Length) bytes"
"install folder: $([int]((Get-ChildItem (Split-Path $qbit) -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)) MB"
for ($round = 1; $round -le $Rounds; ++$round) {
    # Alternating which goes first keeps the swarm's warm-up from favouring one client.
    $order = if ($round % 2 -eq 1) { 'nagare', 'qbittorrent' } else { 'qbittorrent', 'nagare' }
    foreach ($client in $order) {
        try { Run-Round $client $round } catch { "$client round $round failed: $_"; Get-Process nagare, qbittorrent -ErrorAction SilentlyContinue | Where-Object { $_.Path -notmatch 'Programs\\Nagare' } | Stop-Process -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Seconds 5
    }
}
Unregister-ScheduledTask -TaskName NagareBench -Confirm:$false -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName NagareBenchQuit -Confirm:$false -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName NagareBenchShot -Confirm:$false -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName NagareBenchFront -Confirm:$false -ErrorAction SilentlyContinue
Unregister-ScheduledTask -TaskName NagareSearch -Confirm:$false -ErrorAction SilentlyContinue
'bench done'
