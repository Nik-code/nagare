# Runs on the Windows PC: resumes the engine build and prints the first failures.
param([string]$Build = 'C:\dev\engine-fast')
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$script = @"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
"$cmake" --build $Build --target transmission
"@
$bat = Join-Path $env:TEMP 'nagare-engine-rebuild.cmd'
Set-Content -Path $bat -Value $script -Encoding ASCII
$log = & cmd /c "$bat 2>&1"
"exit: $LASTEXITCODE"
$failed = $log | Select-String -Pattern 'FAILED|error |fatal error|Error [0-9]' | Select-Object -First 24
$failed | ForEach-Object { $_.Line.Substring(0, [Math]::Min(320, $_.Line.Length)) }
$log | Select-Object -Last 4 | ForEach-Object { $_.ToString().Substring(0, [Math]::Min(320, $_.ToString().Length)) }
