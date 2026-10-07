<#
.SYNOPSIS
  One-time setup: downloads the ReXGlue SDK, extracts your Earthworm Jim HD
  Xbox Live Arcade package and generates the recompiled C++ sources.

.EXAMPLE
  .\setup.ps1 -Package "D:\Xbox 360\Content\0000000000000000\584109E2\000D0000\8510D3A1..."
  then: ewj\build.bat
#>
param(
    # Path to your own Earthworm Jim HD Xbox Live Arcade package (STFS "LIVE" file).
    [Parameter(Mandatory = $true)][string]$Package
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$sdkVersion = '0.10.0'
$sdkDir = Join-Path $root 'tools\rexglue'
$rexglue = Join-Path $sdkDir 'win-amd64\bin\rexglue.exe'
$game = Join-Path $root 'game'
$assets = Join-Path $root 'ewj\assets'

# 1. ReXGlue SDK (prebuilt release)
if (-not (Test-Path $rexglue)) {
    $zip = Join-Path $env:TEMP "rexglue-sdk-$sdkVersion-win-amd64.zip"
    $url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$sdkVersion/rexglue-sdk-$sdkVersion-win-amd64.zip"
    Write-Host "Downloading ReXGlue SDK $sdkVersion..."
    Invoke-WebRequest -Uri $url -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
}

# 2. Extract the package into game/ and link it as ewj/assets (skipped if already there)
if (-not (Test-Path (Join-Path $game 'default.xex'))) {
    Write-Host "Extracting $Package -> $game"
    & (Join-Path $root 'tools\extract_stfs.ps1') -Package $Package -OutDir $game
}
if (-not (Test-Path (Join-Path $game 'default.xex'))) { throw "default.xex not found after extraction" }
if (-not (Test-Path $assets)) { cmd /c mklink /J "$assets" "$game" | Out-Null }

# 3. Generate recompiled sources (rexglue logs to stderr; judge by exit code)
$ErrorActionPreference = 'Continue'
Push-Location (Join-Path $root 'ewj')
try {
    Write-Host "Running rexglue codegen (a few minutes)..."
    & $rexglue codegen earthworm_jim_hd_manifest.toml 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "codegen failed ($LASTEXITCODE)" }
    # setjmp/longjmp are only called through veneers: rewrite those calls (NOTES.md).
    & cmake "-DGENERATED=$(Join-Path $root 'ewj\generated\default')" -P (Join-Path $root 'tools\patch_setjmp.cmake')
    if ($LASTEXITCODE -ne 0) { throw "patch_setjmp failed ($LASTEXITCODE)" }
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }

Write-Host "`nDone. Build with: ewj\build.bat   Run with: ewj\run.bat"
