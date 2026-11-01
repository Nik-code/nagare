# Runs on the Windows PC: configures with the CMake and Ninja that ship with VS Build Tools, then builds.
param([string]$Source = 'C:\dev\nagare\windows', [string]$Build = 'C:\dev\nagare-build', [string]$Target = 'all')
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$kit = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$script = @"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=$kit;%PATH%
"$cmake" -S "$Source" -B "$Build" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW || exit /b 1
"$cmake" --build "$Build" --target $Target
"@
$bat = Join-Path $env:TEMP 'nagare-build.cmd'
Set-Content -Path $bat -Value $script -Encoding ASCII
& cmd /c $bat
exit $LASTEXITCODE
