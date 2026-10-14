param([Parameter(Mandatory=$true)][string]$ProgramDirectory)
$ErrorActionPreference = 'Stop'
$program = (Resolve-Path -LiteralPath $ProgramDirectory).Path
$exe = Join-Path $program 'FPS悬浮窗.exe'
foreach ($mode in @('--self-test','--verify-picker')) {
    $test = Start-Process -FilePath $exe -ArgumentList $mode -PassThru -Wait -WindowStyle Hidden
    if ($test.ExitCode -ne 0) { throw "验证失败：$mode" }
}
$self = Get-Content -LiteralPath (Join-Path $program 'self-test.json') -Raw | ConvertFrom-Json
$picker = Get-Content -LiteralPath (Join-Path $program 'picker-test.json') -Raw | ConvertFrom-Json
if (!$self.passed -or !$picker.passed -or $picker.editable -or $picker.processCount -lt 1) { throw '统计或进程下拉验证失败' }
$helperRoot = Join-Path $PSScriptRoot ('generic-live-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
& (Join-Path $PSScriptRoot '../build.ps1') -OutputDirectory $helperRoot -RenderTarget
$renderer = Start-Process -FilePath (Join-Path $helperRoot 'FpsTestRenderer.exe') -PassThru
try {
    $test = Start-Process -FilePath $exe -ArgumentList @('--diagnostics','--capture-seconds','12','--process-id', [string]$renderer.Id) -PassThru -WindowStyle Hidden
    if (!$test.WaitForExit(30000)) { Stop-Process -Id $test.Id; throw 'FPS 实际采样验证超时' }
    if ($test.ExitCode -ne 0) { throw 'FPS 实际采样进程退出失败' }
    $live = Get-Content -LiteralPath (Join-Path $program 'status.json') -Raw | ConvertFrom-Json
    if (!$live.valid -or $live.pid -ne $renderer.Id -or $live.totalRows -lt 10 -or $live.error) { throw ('非游戏进程采样未通过：' + ($live | ConvertTo-Json -Compress)) }
    $summary = [ordered]@{ passed=$true; testedAt=[DateTimeOffset]::UtcNow.ToString('o'); statisticsChecks=$self.checks; processPicker=$picker; liveNonGameProcess=@{ process='FpsTestRenderer.exe'; fps=$live.fps; totalRows=$live.totalRows; selectedPidMatched=$true; valid=$live.valid }; networkRequested=$false; gameModified=$false }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $program '验证结果.json') -Encoding utf8
    $summary | ConvertTo-Json -Depth 8
} finally { if (!$renderer.HasExited) { Stop-Process -Id $renderer.Id } }
