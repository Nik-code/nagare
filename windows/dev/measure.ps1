# Runs on the Windows PC: samples RAM, CPU and GPU for a running process over a window.
param([string]$Name = 'nagare', [int]$Seconds = 12)
$proc = Get-Process $Name -ErrorAction Stop | Select-Object -First 1
$id = $proc.Id
$cpu0 = $proc.TotalProcessorTime.TotalSeconds
$start = Get-Date
$engine = @(Get-Counter "\GPU Engine(pid_${id}_*)\Utilization Percentage" -SampleInterval 1 -MaxSamples $Seconds -ErrorAction SilentlyContinue)
$elapsed = ((Get-Date) - $start).TotalSeconds
$proc.Refresh()
$cpu = ($proc.TotalProcessorTime.TotalSeconds - $cpu0) / $elapsed / [Environment]::ProcessorCount * 100
$peak = 0.0
$sum = 0.0
foreach ($sample in $engine) {
    $total = ($sample.CounterSamples | Measure-Object -Property CookedValue -Sum).Sum
    $sum += $total
    if ($total -gt $peak) { $peak = $total }
}
$avg = if ($engine.Count) { $sum / $engine.Count } else { 0 }
$dedicated = (Get-Counter "\GPU Process Memory(pid_${id}_*)\Dedicated Usage" -ErrorAction SilentlyContinue).CounterSamples | Measure-Object -Property CookedValue -Sum
$ws = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$id"
"{0}: private_ws={1:N1}MB ws={2:N1}MB commit={3:N1}MB cpu={4:N2}%% gpu_avg={5:N2}%% gpu_peak={6:N2}%% vram={7:N1}MB threads={8} over {9:N0}s" -f $Name, ($ws.WorkingSetPrivate/1MB), ($proc.WorkingSet64/1MB), ($proc.PrivateMemorySize64/1MB), $cpu, $avg, $peak, ($dedicated.Sum/1MB), $proc.Threads.Count, $elapsed
