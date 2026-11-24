[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Plugin,

    [string]$Config = "Release",

    [switch]$RunTests,

    [string]$Target = "",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$SuiteRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PluginDir  = Join-Path $SuiteRoot "plugins\$Plugin"
$BuildDir   = Join-Path $PluginDir "build"
$DistDir    = Join-Path $SuiteRoot "dist\$Plugin"

if (-not (Test-Path (Join-Path $PluginDir "CMakeLists.txt"))) {
    throw "Plugin CMakeLists.txt not found at: $PluginDir\CMakeLists.txt"
}

$VsRoot    = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$VcVars64  = Join-Path $VsRoot "VC\Auxiliary\Build\vcvars64.bat"
$CmakeDir  = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$NinjaDir  = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"

if (-not (Test-Path $VcVars64)) {
    throw "vcvars64.bat not found at: $VcVars64"
}

function Import-VcVars64 {
    param([string]$BatPath)
    if ($env:VSCMD_ARG_TGT_ARCH -eq "x64" -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        return
    }
    Write-Host "[build_plugin] Importing VS 2022 Build Tools x64 environment..." -ForegroundColor Cyan
    $cmdLine = "`"$BatPath`" >nul && set"
    $lines = & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars64.bat failed with exit code $LASTEXITCODE"
    }
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) {
            $name = $line.Substring(0, $eq)
            $val  = $line.Substring($eq + 1)
            [System.Environment]::SetEnvironmentVariable($name, $val, "Process")
        }
    }
}

Import-VcVars64 -BatPath $VcVars64

# Ensure VS CMake and Ninja are first on PATH
$extraPaths = @($CmakeDir, $NinjaDir) | Where-Object { Test-Path $_ }
$env:PATH = (($extraPaths -join ";") + ";" + $env:PATH)

$CmakeExe = Join-Path $CmakeDir "cmake.exe"
$NinjaExe = Join-Path $NinjaDir "ninja.exe"

if (-not (Test-Path $CmakeExe)) { $CmakeExe = "cmake" }
if (-not (Test-Path $NinjaExe)) { $NinjaExe = "ninja" }

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[build_plugin] Cleaning build directory: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "[build_plugin] Configuring $Plugin ($Config)..." -ForegroundColor Cyan
& $CmakeExe -S $PluginDir -B $BuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_MAKE_PROGRAM=$NinjaExe"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for $Plugin (exit $LASTEXITCODE)"
}

$buildArgs = @("--build", $BuildDir, "--config", $Config)
if ($Target -ne "") {
    $buildArgs += @("--target", $Target)
    Write-Host "[build_plugin] Building target '$Target' for $Plugin ($Config)..." -ForegroundColor Cyan
} else {
    Write-Host "[build_plugin] Building all targets for $Plugin ($Config)..." -ForegroundColor Cyan
}

& $CmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed for $Plugin (exit $LASTEXITCODE)"
}

# Run tests if requested
if ($RunTests) {
    $testCandidates = Get-ChildItem -Path $BuildDir -Recurse -File -Filter "*Tests.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "[\\/]_deps[\\/]" -and $_.FullName -notmatch "[\\/]CMakeFiles[\\/]" }

    $preferred = $testCandidates | Where-Object { $_.Name -ieq "${Plugin}Tests.exe" } | Select-Object -First 1
    if (-not $preferred) {
        $preferred = $testCandidates | Select-Object -First 1
    }

    if ($preferred) {
        Write-Host "[build_plugin] Running tests: $($preferred.FullName)" -ForegroundColor Cyan
        & $preferred.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed for $Plugin (exit $LASTEXITCODE)"
        }
        Write-Host "[build_plugin] Tests PASSED for $Plugin." -ForegroundColor Green
    } else {
        Write-Warning "[build_plugin] -RunTests specified, but no *Tests.exe found under $BuildDir"
    }
}

# Collect and copy build artifacts into dist\<Plugin>\
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$copied = [System.Collections.Generic.List[string]]::new()

# 1. VST3 bundles (*.vst3 directories or files under *_artefacts)
$vst3Items = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.vst3" -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch "[\\/]_deps[\\/]" -and
        $_.FullName -notmatch "[\\/]CMakeFiles[\\/]" -and
        $_.Parent.Name -ieq "VST3"
    }
foreach ($item in $vst3Items) {
    $dest = Join-Path $DistDir $item.Name
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Copy-Item -Path $item.FullName -Destination $dest -Recurse -Force
    $copied.Add($dest)
}

# 2. CLAP plugins (*.clap files under *_artefacts or build)
$clapItems = Get-ChildItem -Path $BuildDir -Recurse -File -Filter "*.clap" -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch "[\\/]_deps[\\/]" -and
        $_.FullName -notmatch "[\\/]CMakeFiles[\\/]"
    }
foreach ($item in $clapItems) {
    $dest = Join-Path $DistDir $item.Name
    Copy-Item -Path $item.FullName -Destination $dest -Force
    $copied.Add($dest)
}

# 3. Standalone executables under *\Standalone\*.exe
$standaloneItems = Get-ChildItem -Path $BuildDir -Recurse -File -Filter "*.exe" -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch "[\\/]_deps[\\/]" -and
        $_.FullName -notmatch "[\\/]CMakeFiles[\\/]" -and
        $_.Directory.Name -ieq "Standalone"
    }
foreach ($item in $standaloneItems) {
    $dest = Join-Path $DistDir $item.Name
    Copy-Item -Path $item.FullName -Destination $dest -Force
    $copied.Add($dest)
}

Write-Host "[build_plugin] Artifacts staged in $DistDir :" -ForegroundColor Green
if ($copied.Count -eq 0) {
    Write-Warning "No .vst3, .clap, or Standalone .exe artifacts found under $BuildDir"
} else {
    foreach ($c in $copied) {
        Write-Host "  - $c" -ForegroundColor White
    }
}
