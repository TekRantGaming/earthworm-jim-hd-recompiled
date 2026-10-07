<#
.SYNOPSIS
  Post-codegen fix: setjmp/longjmp calls that go through veneers.

  ReXGlue turns a call to setjmp_address / longjmp_address (overrides.toml)
  into an inline ppc_setjmp / ppc_longjmp, which has to happen in the caller's
  own frame. This game never calls them directly: every call goes through one
  of two long-branch veneers each, so the generator emitted plain calls and
  the guest longjmp in libjpeg's error handler restored guest registers
  without unwinding the host stack (crash: read of guest 0x1A4).

  This rewrites each call to those veneers in the generated sources into the
  same code ReXGlue emits for a direct call. setup.ps1 runs it after every
  `rexglue codegen`. Running it twice is harmless (nothing left to rewrite).
#>
param([string]$Generated = (Join-Path $PSScriptRoot '..\ewj\generated\default'))
$ErrorActionPreference = 'Stop'

$setjmp = @('827CFFC0', '837E4D40')   # -> setjmp 0x83259AE0
$longjmp = @('82728C90', '8373DA10')  # -> longjmp 0x832596B0
$pattern = '(?m)^(\t+)sub_(' + (($setjmp + $longjmp) -join '|') + ')\(ctx, base\);\r?$'

$total = 0
$evaluator = {
    param($m)
    $indent = $m.Groups[1].Value; $target = $m.Groups[2].Value
    if ($longjmp -contains $target) {
        return "${indent}ppc_longjmp(ctx.r3.u32, ctx.r4.s32);  // ewj: longjmp via veneer $target"
    }
    # Same as BuilderContext::emit_function_call for setjmp_address.
    return ("${indent}{  // ewj: setjmp via veneer $target`n" +
            "${indent}`tPPCContext ewj_env = ctx;`n" +
            "${indent}`tPPCRegister ewj_ret{};`n" +
            "${indent}`tewj_ret.s64 = ppc_setjmp(ctx.r3.u32);`n" +
            "${indent}`tif (ewj_ret.s64 != 0) ctx = ewj_env;`n" +
            "${indent}`tctx.r3 = ewj_ret;`n" +
            "${indent}}")
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($file in Get-ChildItem -Path $Generated -Filter '*_recomp.*.cpp') {
    $text = [IO.File]::ReadAllText($file.FullName)
    $count = [regex]::Matches($text, $pattern).Count
    if ($count -eq 0) { continue }
    $new = [regex]::Replace($text, $pattern, [System.Text.RegularExpressions.MatchEvaluator]$evaluator)
    [IO.File]::WriteAllText($file.FullName, $new, $utf8)
    $total += $count
}
Write-Host "patch_setjmp: rewrote $total call sites"
