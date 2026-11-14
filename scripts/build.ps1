# Configures and builds the project with the installed Visual Studio Build
# Tools, then runs the unit tests.
#
#   .\scripts\build.ps1                    # RelWithDebInfo + tests
#   .\scripts\build.ps1 -Config Debug
#   .\scripts\build.ps1 -SkipTests
#
# Visual Studio 2026's compiler (MSVC 14.51.36231, cl 19.51.36260) generates wrong code for this project at /O2
# (docs\research\msvc-14.51-miscompila.md), so a new build folder is configured with Visual Studio 2022 even when 2026
# is installed too, and 2026 alone is refused. -AllowVS2026 uses it anyway (to check one of its updates).

param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'RelWithDebInfo',
    [switch]$SkipTests,
    [switch]$AllowVS2026
)
# Native tools write warnings to stderr; with 'Stop' PowerShell 5.1 would turn
# those into terminating errors. Failures are detected via $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cache = Join-Path $buildDir 'CMakeCache.txt'
$generator = $null
$vs = $null
$vs2026Why = "its C++ compiler (MSVC 14.51) generates wrong code for this project " +
    "(docs\research\msvc-14.51-miscompila.md)"
# The newest installation with the C++ x64 toolset, within a vswhere version range if one is given.
function Find-VisualStudio([string[]]$Range = @()) {
    $json = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 @Range -format json
    return $json | ConvertFrom-Json | Select-Object -First 1
}
if (Test-Path -LiteralPath $cache) {
    # A configured tree is tied to both its generator and VS instance. In particular,
    # CMake can use a newer VS installation to service an older VS generator, so do
    # not replace either choice just because vswhere reports a newer installation.
    foreach ($line in Get-Content -LiteralPath $cache) {
        if ($line -match '^CMAKE_GENERATOR:INTERNAL=(.+)$') { $generator = $Matches[1] }
        if ($line -match '^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$') { $vs = $Matches[1] }
    }
    if (-not $vs -or -not (Test-Path -LiteralPath $vs)) {
        throw "The configured Visual Studio instance no longer exists: $vs. Remove '$buildDir' and configure again."
    }
    # The generator decides the compiler ("Visual Studio 17 2022" is MSVC 14.4x whichever instance runs it).
    if ($generator -eq 'Visual Studio 18 2026' -and -not $AllowVS2026) {
        throw ("'$buildDir' was configured for Visual Studio 2026: $vs2026Why. Remove that folder and run this again " +
            "(it then uses Visual Studio 2022), or pass -AllowVS2026 to build with it anyway.")
    }
} else {
    # A new build folder: Visual Studio 2022 when it is installed, even next to a newer one.
    $newest = Find-VisualStudio
    if (-not $newest) { throw 'Visual Studio / Build Tools with the C++ x64 toolset not found' }
    $instance = Find-VisualStudio @('-version', '[17.0,18.0)')
    if ($AllowVS2026 -or -not $instance) { $instance = $newest }
    $vs = $instance.installationPath
    $major = [int]($instance.installationVersion.Split('.')[0])
    if ($major -eq 18 -and -not $AllowVS2026) {
        throw ("Only Visual Studio 2026 was found ($vs): $vs2026Why. Install the Visual Studio 2022 Build Tools " +
            "(MSVC v143) and run this again, or pass -AllowVS2026 to build with it anyway.")
    }
    $generator = switch ($major) {
        18 { 'Visual Studio 18 2026' }
        17 { 'Visual Studio 17 2022' }
        default { throw "Unsupported Visual Studio major version $major at $vs" }
    }
}
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ctest)) {
    throw "The CMake tools were not found in the selected Visual Studio instance: $vs"
}

$configure = @('-S', $root, '-B', $buildDir, '-Wno-dev', '-Wno-deprecated')
if (-not (Test-Path -LiteralPath $cache)) { $configure += @('-G', $generator, '-A', 'x64') }
& $cmake @configure
if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }

& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

if (-not $SkipTests) {
    # The script tests run in Windows PowerShell 5.1. Started from PowerShell 7 they would inherit its PSModulePath, and
    # 5.1 then cannot load its own modules (Get-FileHash "not recognized"): give them none, so 5.1 uses its defaults.
    $savedModulePath = $env:PSModulePath
    if ($PSVersionTable.PSEdition -eq 'Core') { Remove-Item Env:PSModulePath -ErrorAction SilentlyContinue }
    try {
        & $ctest --test-dir $buildDir -C $Config --output-on-failure
    } finally {
        $env:PSModulePath = $savedModulePath
    }
    if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
}

Write-Host "Build OK: $root\build\bin\$Config"
