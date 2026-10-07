param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$OutDir
)
Add-Type -Path (Join-Path $PSScriptRoot 'StfsExtract.cs')
[StfsExtract]::Run((Resolve-Path $Package).Path, [IO.Path]::GetFullPath($OutDir)) | Out-Null
