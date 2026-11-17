$ErrorActionPreference = "Stop"

# Refresh PATH from Machine + User environment in case LLVM-MinGW was just installed
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")

$root = $PSScriptRoot
$buildDir = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$resObj = Join-Path $buildDir "app.res"
$outExe = Join-Path $buildDir "TaskbarMonitor.exe"

Push-Location (Join-Path $root "res")
try {
    & llvm-rc /fo $resObj app.rc
    if ($LASTEXITCODE -ne 0) { throw "llvm-rc failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$includeDirs = @(
    "-I$(Join-Path $root 'src')",
    "-I$(Join-Path $root 'res')"
)

$sources = @(
    (Join-Path $root "src\main.cpp"),
    (Join-Path $root "src\app\app_controller.cpp"),
    (Join-Path $root "src\app\tray_icon.cpp"),
    (Join-Path $root "src\app\widget_window.cpp"),
    (Join-Path $root "src\app\flyout_window.cpp"),
    (Join-Path $root "src\app\menu_window.cpp"),
    (Join-Path $root "src\core\config.cpp"),
    (Join-Path $root "src\collectors\sensors.cpp"),
    (Join-Path $root "src\collectors\cpu_sensor.cpp"),
    (Join-Path $root "src\collectors\gpu_sensor.cpp"),
    (Join-Path $root "src\collectors\net_ram_sensor.cpp"),
    (Join-Path $root "src\collectors\gold_price.cpp"),
    (Join-Path $root "src\render\draw_primitives.cpp"),
    (Join-Path $root "src\render\render_engine.cpp"),
    (Join-Path $root "src\render\widget_renderer.cpp"),
    (Join-Path $root "src\render\flyout_renderer.cpp"),
    (Join-Path $root "src\render\menu_renderer.cpp"),
    (Join-Path $root "src\taskbar\taskbar_host.cpp"),
    (Join-Path $root "src\taskbar\taskbar_layout.cpp")
)

$libs = @(
    "-ld2d1",
    "-ldwrite",
    "-ld3d11",
    "-ldcomp",
    "-lwindowscodecs",
    "-liphlpapi",
    "-lpowrprof",
    "-lpdh",
    "-ldxgi",
    "-lshell32",
    "-lole32",
    "-loleaut32",
    "-luuid",
    "-ladvapi32",
    "-lwinhttp"
)

& clang++ -std=c++20 -O2 -Wall -Wextra -municode -mwindows -static @includeDirs @sources $resObj -o $outExe @libs
if ($LASTEXITCODE -ne 0) { throw "clang++ failed with exit code $LASTEXITCODE" }

$item = Get-Item $outExe
Write-Host "Build succeeded: $($item.FullName) ($([Math]::Round($item.Length / 1KB, 1)) KB)" -ForegroundColor Green
