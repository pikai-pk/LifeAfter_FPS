param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$fpsRoot = [IO.Path]::GetFullPath($GameRoot).TrimEnd('\') + '\'
$fpsProcesses = @(Get-Process -Name lifeafter,mingrizhihou,mrzh_launcher -ErrorAction SilentlyContinue | Where-Object {
    try { $fpsPath = $_.Path } catch { $fpsPath = $null }
    (-not $fpsPath) -or $fpsPath.StartsWith($fpsRoot, [StringComparison]::OrdinalIgnoreCase)
} | Select-Object ProcessName,Id)
ConvertTo-Json -InputObject $fpsProcesses -Compress
