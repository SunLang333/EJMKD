param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 Build Tools."
}

$installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) {
    throw "Visual Studio Build Tools with MSVC were not found."
}

$cmake = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) {
    throw "cmake.exe was not found under the Visual Studio installation."
}

$buildDir = Join-Path $root 'build'
& $cmake -S $root -B $buildDir -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& $cmake --build $buildDir --config $Config --target ejmdk
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$exePath = Join-Path $buildDir "$Config\ejmdk.exe"
if (-not (Test-Path $exePath)) {
    throw "Build completed without producing $exePath."
}

Write-Host "Built executable: $exePath"
Write-Host "Run example:"
Write-Host "  $exePath sample-5.mp4"
