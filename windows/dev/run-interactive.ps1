# Runs on the Windows PC: starts a program on the signed-in desktop, since an SSH session has no display.
param([string]$Exe, [string]$Arguments = '', [int]$WaitSeconds = 8, [string]$Task = 'NagareDev')
$task = $Task
schtasks /Delete /TN $task /F 2>$null | Out-Null
$action = if ($Arguments) { New-ScheduledTaskAction -Execute $Exe -Argument $Arguments } else { New-ScheduledTaskAction -Execute $Exe }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $task
Start-Sleep -Seconds $WaitSeconds
$info = Get-ScheduledTaskInfo -TaskName $task
"state=$((Get-ScheduledTask -TaskName $task).State) lastResult=$($info.LastTaskResult)"
