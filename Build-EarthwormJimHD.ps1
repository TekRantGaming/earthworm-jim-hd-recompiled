<#
  Earthworm Jim HD builder.

  Builds the PC port on this computer from your own Earthworm Jim HD Xbox Live
  Arcade package. Nothing from the game is downloaded or included: the game
  code is translated and compiled here, from your file.

  Steps: check build tools (offer to install them), pick your package,
  download the ReXGlue SDK, translate the game code, compile, and put the
  finished game in the EarthwormJimHD folder.
#>
param(
    [string]$Package,                          # skip the file picker
    [string]$OutDir = "$PSScriptRoot\EarthwormJimHD",
    [switch]$Yes,                              # answer yes to every question
    [switch]$NoShortcut,                       # never add a desktop shortcut
    [switch]$NoLaunch                          # do not offer to start the game
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$titleId = 0x584109E2

function Say($text, $color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Step($n, $text) { Write-Host ""; Write-Host " $n  $text" -ForegroundColor Green }
function Ask($question) {
    if ($Yes) { return $true }
    $a = Read-Host "$question [Y/n]"
    return ($a -eq '' -or $a -match '^[Yy]')
}
function Fail($text) { Write-Host ""; Write-Host " $text" -ForegroundColor Red; if (-not $Yes) { Read-Host "Press Enter to close" }; exit 1 }

Clear-Host
Say ""
Say "  EARTHWORM JIM HD  -  PC port builder" 'Green'
Say "  Builds the game on this PC from your own Xbox Live Arcade package."
Say "  This takes about 15 to 30 minutes and needs about 5 GB free."

# ---------------------------------------------------------------- 1. tools ---
Step 1 "Checking build tools"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function Find-VS { if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath } }
function Have($exe, $fallback) { (Get-Command $exe -ErrorAction SilentlyContinue) -or ($fallback -and (Test-Path $fallback)) }

$missing = @()
if (-not (Find-VS)) { $missing += 'vs' }
if (-not (Have cmake "$env:ProgramFiles\CMake\bin\cmake.exe")) { $missing += 'cmake' }
if (-not (Have ninja "")) { $missing += 'ninja' }

if ($missing.Count) {
    Say "  Missing:" 'Yellow'
    if ($missing -contains 'vs') { Say "   - Visual Studio 2022 Build Tools with C++ and Clang (about 6 GB)" 'Yellow' }
    if ($missing -contains 'cmake') { Say "   - CMake" 'Yellow' }
    if ($missing -contains 'ninja') { Say "   - Ninja" 'Yellow' }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is not available. Install the tools above yourself, then run this again." }
    if (-not (Ask "  Install them now with winget? Windows will ask for permission")) { Fail "The build tools are needed. Install them, then run this again." }
    if ($missing -contains 'cmake') { winget install --id Kitware.CMake -e --accept-package-agreements --accept-source-agreements --silent }
    if ($missing -contains 'ninja') { winget install --id Ninja-build.Ninja -e --accept-package-agreements --accept-source-agreements --silent }
    if ($missing -contains 'vs') {
        Say "  Installing Visual Studio Build Tools. This can take a while." 'Yellow'
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
    }
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not (Find-VS)) { Fail "Visual Studio Build Tools still not found. Restart your PC and run this again." }
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $env:Path += ";$env:ProgramFiles\CMake\bin" }
if (-not (Test-Path "$root\third_party\trg-launcher\CMakeLists.txt")) {
    Say "  Fetching the TRG launcher (git submodule)..."
    git -C $root submodule update --init --recursive
    if (-not (Test-Path "$root\third_party\trg-launcher\CMakeLists.txt")) { Fail "third_party\trg-launcher is missing. Download the project with: git clone --recursive" }
}
Say "  Build tools ready." 'Green'

# -------------------------------------------------------------- 2. package ---
Step 2 "Choosing your Earthworm Jim HD package"
if (-not $Package) {
    Say "  Pick your Earthworm Jim HD Xbox Live Arcade package. On an Xbox 360 drive it is the file"
    Say "  with a long hexadecimal name in Content\0000000000000000\584109E2\000D0000."
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = "Select your Earthworm Jim HD Xbox Live Arcade package"
    $dlg.Filter = 'All files|*.*'
    if ($dlg.ShowDialog() -ne 'OK') { Fail "No package chosen." }
    $Package = $dlg.FileName
}
if (-not (Test-Path -LiteralPath $Package)) { Fail "File not found: $Package" }
Add-Type -Path "$root\tools\StfsExtract.cs"
$id = [StfsExtract]::TitleId((Resolve-Path -LiteralPath $Package).Path)
if ($id -eq 0) { Fail "That file is not an Xbox 360 content package (LIVE/PIRS/CON)." }
if ($id -ne $titleId) { Fail ("That package is title {0:X8}, not Earthworm Jim HD ({1:X8})." -f $id, $titleId) }
$drive = (Get-Item -LiteralPath $root).PSDrive
if ($drive.Free -lt 5GB) { Fail ("About 5 GB of free space is needed on {0}: (found {1:N1} GB)." -f $drive.Name, ($drive.Free / 1GB)) }
Say "  Found Earthworm Jim HD ($([IO.Path]::GetFileName($Package)))." 'Green'

# ------------------------------------------------- 3. extract and translate ---
Step 3 "Unpacking the package and translating the game code"
& "$root\setup.ps1" -Package $Package
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { Fail "Setup failed (see above)." }

# -------------------------------------------------------------- 4. compile ---
Step 4 "Compiling (the long part)"
cmd /c "`"$root\ewj\build.bat`" ewj-release"
if ($LASTEXITCODE -ne 0) { Fail "Compiling failed (see above)." }

# ---------------------------------------------------------------- 5. stage ---
Step 5 "Putting the game together"
$bin = "$root\ewj\out\build\ewj-release"
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item "$bin\*.exe", "$bin\*.dll" $OutDir -Force
# Move (not copy) the game files so they aren't stored twice. ewj\assets is a
# junction to game\; drop it (rmdir removes the link, not the files).
if (Test-Path "$OutDir\game") { Move-Item "$OutDir\game" "$OutDir\game.old" -Force }
cmd /c "rmdir `"$root\ewj\assets`"" 2>$null
Move-Item "$root\game" "$OutDir\game"
$exe = "$OutDir\earthworm_jim_hd.exe"
Say "  Done: $exe" 'Green'

if (-not $NoShortcut -and (Ask "  Add a desktop shortcut?")) {
    $lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Earthworm Jim HD.lnk'
    $sh = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $sh.TargetPath = $exe; $sh.WorkingDirectory = $OutDir; $sh.Save()
    Say "  Shortcut added." 'Green'
}
Say ""
Say "  All done. Hold Shift while starting the game to open the launcher at any time." 'Green'
if (-not $NoLaunch -and (Ask "  Start Earthworm Jim HD now?")) { Start-Process $exe -WorkingDirectory $OutDir }
