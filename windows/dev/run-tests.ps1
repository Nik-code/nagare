# Runs on the Windows PC: every self test on the signed-in desktop, one after another, and prints pass or fail.
param([string]$Slot = 'main')
$exe = "C:\dev\slots\$Slot\build\nagare.exe"
$runner = "C:\dev\slots\$Slot\src\dev\run-interactive.ps1"
$tests = @(
    @{ name = 'widgets'; args = '--widgets-test' },
    @{ name = 'main'; args = '--demo --main-test' },
    @{ name = 'panels'; args = '--demo --panels-test' },
    @{ name = 'details'; args = '--demo --details-test' },
    @{ name = 'settings'; args = '--demo --settings-test' },
    @{ name = 'shell'; args = "--shell-test C:\dev\slots\$Slot\shell-test" }
)
$task = "Nagare$Slot"
foreach ($test in $tests) {
    & $runner -Exe $exe -Arguments $test.args -WaitSeconds 1 -Task $task | Out-Null
    $deadline = (Get-Date).AddSeconds(120)
    do {
        Start-Sleep -Seconds 1
        $state = (Get-ScheduledTask -TaskName $task).State
    } while ($state -eq 'Running' -and (Get-Date) -lt $deadline)
    $code = (Get-ScheduledTaskInfo -TaskName $task).LastTaskResult
    if ($state -eq 'Running') { Stop-ScheduledTask -TaskName $task; $code = 'timeout' }
    "$($test.name): exit $code"
}
Get-ChildItem "C:\dev\slots\$Slot\build" -Filter *-test.log | ForEach-Object {
    "--- $($_.Name)"
    Get-Content $_.FullName | Select-String -Pattern 'FAIL|failed' | Select-Object -First 12 | ForEach-Object { $_.Line }
}
Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
