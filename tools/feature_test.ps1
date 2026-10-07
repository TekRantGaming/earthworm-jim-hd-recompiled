<#
.SYNOPSIS
  Runs the game once per launcher setting (forced on the command line, launcher
  auto-played) and collects what the log reports plus a screenshot of the window.
  Developer test aid for the launcher's feature set.

.EXAMPLE
  .\tools\feature_test.ps1 -Seconds 14
#>
param(
    [int]$Seconds = 14,
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'ewj\out\build\ewj-relwithdebinfo'
$exe = Join-Path $dir 'earthworm_jim_hd.exe'
$shots = Join-Path $root 'screenshots\features'
New-Item -ItemType Directory -Force $shots | Out-Null

$cases = [ordered]@{
    'quality_native'      = @('--ewj_render_quality=native')
    'quality_quality'     = @('--ewj_render_quality=quality')
    'quality_performance' = @('--ewj_render_quality=performance')
    'quality_supersample' = @('--ewj_render_quality=supersample')
    'custom_scale2'       = @('--ewj_render_quality=custom', '--resolution_scale=2')
    'window_1280x720'     = @('--fullscreen=false', '--window_width=1280', '--window_height=720')
    'fullscreen'          = @('--fullscreen=true')
    'stretch'             = @('--fullscreen=false', '--present_letterbox=false', '--window_width=1600', '--window_height=1200')
    'fxaa_extreme'        = @('--swap_post_effect=fxaa_extreme')
    'msaa_off_aniso16'    = @('--native_2x_msaa=false', '--anisotropic_override=5')
    'lang_french'         = @('--user_language=4')
    'lang_japanese'       = @('--user_language=2')
    'trial'               = @('--license_mask=0')
    'fps_counter'         = @('--ewj_show_fps=true')
}
$env:TRG_LAUNCHER_AUTOPLAY = '1'
foreach ($name in $cases.Keys) {
    if ($Only.Count -and $Only -notcontains $name) { continue }
    $log = "feature_$name.log"
    $args2 = @("--game_data_root=`"$(Join-Path $root 'ewj\assets')`"", "--log_file=$log") + $cases[$name]
    $p = Start-Process -FilePath $exe -ArgumentList $args2 -WorkingDirectory $dir -PassThru
    Start-Sleep $Seconds
    $alive = -not $p.HasExited
    if ($alive) {
        try { & (Join-Path $PSScriptRoot 'snap_window.ps1') -ProcessId $p.Id -OutFile (Join-Path $shots "$name.png") | Out-Null } catch {}
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep 2
    $lines = Get-Content (Join-Path $dir $log) -ErrorAction SilentlyContinue
    $facts = $lines | Select-String 'draw resolution scale|render preset|could not set|Unhandled|critical|Guest FPS' |
             Select-Object -Last 3 | ForEach-Object { ($_.Line -replace '^\[[^\]]*\] \[[^\]]*\] \[[^\]]*\] \[[^\]]*\] ', '') }
    "{0,-20} alive={1}  {2}" -f $name, $alive, ($facts -join ' | ')
}
$env:TRG_LAUNCHER_AUTOPLAY = $null
