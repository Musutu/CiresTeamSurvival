param([string]$CMakePath = 'cmake')
$ErrorActionPreference = 'Stop'
if (-not (Get-Command $CMakePath -ErrorAction SilentlyContinue)) {
    & (Join-Path $PSScriptRoot 'Run-MSVC.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'MSVC gameplay rules tests failed.' }
    return
}
$buildDirectory = Join-Path $PSScriptRoot 'build'
& $CMakePath -S $PSScriptRoot -B $buildDirectory
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed. Install a C++17 compiler and CMake.' }
& $CMakePath --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Rules test compilation failed.' }
$ctestPath = Join-Path (Split-Path (Get-Command $CMakePath).Source) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath)) { $ctestPath = 'ctest' }
& $ctestPath --test-dir $buildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Rules tests failed.' }
