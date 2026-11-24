[CmdletBinding()]
param(
    [string]$JuceTag = "8.0.6"
)

$ErrorActionPreference = "Stop"
$SuiteRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ThirdParty = Join-Path $SuiteRoot "third_party"
New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null

$JuceDir = Join-Path $ThirdParty "JUCE"
if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
    Write-Host "[setup_deps] Cloning JUCE $JuceTag..." -ForegroundColor Cyan
    git clone --depth 1 --branch $JuceTag https://github.com/juce-framework/JUCE.git $JuceDir
} else {
    Write-Host "[setup_deps] JUCE already present at $JuceDir" -ForegroundColor Green
}

$ClapDir = Join-Path $ThirdParty "clap-juce-extensions"
if (-not (Test-Path (Join-Path $ClapDir "CMakeLists.txt"))) {
    Write-Host "[setup_deps] Cloning clap-juce-extensions..." -ForegroundColor Cyan
    git clone --depth 1 --recursive https://github.com/free-audio/clap-juce-extensions.git $ClapDir
} else {
    Write-Host "[setup_deps] clap-juce-extensions already present at $ClapDir" -ForegroundColor Green
}

Write-Host "[setup_deps] Dependencies ready. Build any plugin with:" -ForegroundColor Green
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\build_plugin.ps1 -Plugin SessionMate -RunTests" -ForegroundColor White
