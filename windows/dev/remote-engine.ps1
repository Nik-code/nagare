# Runs on the Windows PC: fetches the engine source and its dependencies, then builds libtransmission statically.
# The engine is built for speed, not size: every byte it moves goes through this code.
param([string]$Root = 'C:\dev', [string]$Build = 'engine-fast')
$ErrorActionPreference = 'Continue'
$src = "$Root\nagare-src"
$vcpkg = "$Root\vcpkg"
$out = "$Root\$Build"
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$flags = '/MT /O2 /Ob2 /DNDEBUG'

if (-not (Test-Path "$src\.git")) {
    git clone --quiet --branch nagare --filter=blob:none https://github.com/Nik-code/nagare.git $src
}
git -C $src submodule update --init --quiet --depth 1 third-party/dht third-party/libb64 third-party/libevent third-party/libnatpmp third-party/libutp third-party/miniupnp third-party/utfcpp third-party/libdeflate third-party/libpsl third-party/fmt third-party/fast_float third-party/wide-integer third-party/small third-party/rapidjson third-party/rpavlik-cmake-modules

git -C "$src\third-party\libpsl" submodule update --init --quiet --depth 1

if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
    git clone --quiet --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
    & "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics
}
& "$vcpkg\vcpkg.exe" install --triplet x64-windows-static curl mbedtls zlib --disable-metrics
if ($LASTEXITCODE -ne 0) { "vcpkg failed: $LASTEXITCODE"; exit 1 }

$lines = @(
    '@echo off',
    "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul",
    "`"$cmake`" -S `"$src`" -B `"$out`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=`"$vcpkg\scripts\buildsystems\vcpkg.cmake`" -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DENABLE_DAEMON=OFF -DENABLE_UTILS=OFF -DENABLE_QT=OFF -DENABLE_GTK=OFF -DENABLE_MAC=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF -DENABLE_NLS=OFF -DINSTALL_WEB=OFF -DINSTALL_DOC=OFF -DRUN_CLANG_TIDY=OFF -DWITH_CRYPTO=mbedtls || exit /b 1",
    "`"$cmake`" --build `"$out`" --target transmission || exit /b 1"
)
# The engine's ExternalProject forwards CMAKE_C_FLAGS but not the per-config flags, so these default to
# /MD and can't link into a single static nagare.exe.
foreach ($dir in 'dht.bld\src\dht-build', 'libdeflate.bld\src\deflatestatic-build', 'libnatpmp.bld\src\natpmp-build', 'libpsl.bld\src\psl-build', 'miniupnp\miniupnpc.bld\src\libminiupnpc-build') {
    $full = "$out\third-party\$dir"
    $lines += "`"$cmake`" -DCMAKE_C_FLAGS_RELEASE=`"$flags`" -DCMAKE_CXX_FLAGS_RELEASE=`"$flags`" `"$full`" >nul || exit /b 1"
    $lines += "`"$cmake`" --build `"$full`" --clean-first --target install || exit /b 1"
}
$bat = Join-Path $env:TEMP 'nagare-engine.cmd'
Set-Content -Path $bat -Value ($lines -join "`r`n") -Encoding ASCII
& cmd /c $bat
"engine exit: $LASTEXITCODE"
Get-ChildItem $out -Recurse -Filter *.lib -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Length) $($_.FullName)" }
