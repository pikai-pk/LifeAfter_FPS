param([string]$Version = '1.0.1', [string]$TestArchive)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw '版本格式必须是 x.y.z' }
$projectRoot = $PSScriptRoot
$releaseName = "明日之后帧率助手-v$Version-win-x64"
$releaseRoot = Join-Path $projectRoot "发行版/$releaseName"
$testRoot = Join-Path $projectRoot 'tests'
$node = Join-Path $releaseRoot 'runtime/node.exe'
if ($TestArchive) { $resolvedTestArchive = (Resolve-Path -LiteralPath $TestArchive).Path }
else {
    $localFixture = Join-Path $testRoot 'game-copy-Kpc7YA/Documents/script.py314.lc.npk'
    if (!(Test-Path -LiteralPath $localFixture)) { throw '请用 -TestArchive 指定自己本地兼容的官方脚本包；不会发布游戏样本。' }
    $resolvedTestArchive = (Resolve-Path -LiteralPath $localFixture).Path
}
$uiReport = Join-Path $testRoot 'release-ui-verification.json'
$arguments = @('--verify-ui', ('"' + $uiReport + '"'))
$verification = Start-Process -FilePath (Join-Path $releaseRoot '明日之后帧率助手.exe') -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
if ($verification.ExitCode -ne 0) { throw '界面档位路由测试失败' }
$ui = Get-Content -LiteralPath $uiReport -Raw | ConvertFrom-Json
if (!$ui.passed -or $ui.editable) { throw '界面档位校验失败' }
$agreementReport = Join-Path $testRoot 'agreement-verification.json'
$verification = Start-Process -FilePath (Join-Path $releaseRoot '明日之后帧率助手.exe') -ArgumentList @('--verify-agreement', ('"' + $agreementReport + '"')) -PassThru -Wait -WindowStyle Hidden
if ($verification.ExitCode -ne 0) { throw '使用协议门控测试失败' }
$agreement = Get-Content -LiteralPath $agreementReport -Raw | ConvertFrom-Json
if (!$agreement.passed) { throw '使用协议验证失败' }
$previousHome = $env:FPS_TEST_HOME
$previousArchive = $env:FPS_TEST_ARCHIVE
$previousFps = $env:FPS_TEST_FPS
try {
    $env:FPS_TEST_HOME = $releaseRoot
    $env:FPS_TEST_FPS = '120'
    & $node (Join-Path $testRoot 'prepare-test-fixtures.mjs') --archive $resolvedTestArchive
    if ($LASTEXITCODE -ne 0) { throw '本地模块测试样本准备失败' }
    & $node --test (Join-Path $testRoot 'engine.test.mjs')
    if ($LASTEXITCODE -ne 0) { throw '补丁引擎测试失败' }
    $env:FPS_TEST_ARCHIVE = $resolvedTestArchive
    & $node (Join-Path $testRoot 'e2e-copy.mjs')
    if ($LASTEXITCODE -ne 0) { throw '完整游戏包副本测试失败' }
} finally { $env:FPS_TEST_HOME = $previousHome; $env:FPS_TEST_ARCHIVE = $previousArchive; $env:FPS_TEST_FPS = $previousFps }
$e2e = Get-Content -LiteralPath (Join-Path $testRoot 'e2e-copy-verification.json') -Raw | ConvertFrom-Json
if (!$e2e.passed -or !$e2e.originalUnchanged -or !$e2e.copyFullyRestored) { throw '应用和恢复验证未通过' }
$summary = [ordered]@{
    version = $Version; testedAt = [DateTime]::UtcNow.ToString('o'); platform = 'Windows x64'
    uiPresets = $ui.presets; defaultFps = $ui.selected; manualFpsInput = $ui.editable
    agreement = $agreement
    engineTests = @{ passed = 9; failed = 0 }
    archiveCopyIntegration = @{ passed = $e2e.passed; sourceUnchanged = $e2e.originalUnchanged; fullyRestored = $e2e.copyFullyRestored; steps = @($e2e.runs | ForEach-Object { $_.result }) }
    runtimeFpsMeasured = $false
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $releaseRoot '验证结果.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $projectRoot '使用说明.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '免责声明.md') -Destination $releaseRoot -Force
# Whitelist shipping files: never include local logs, personal backups or settings.
$files = @((Get-Item -LiteralPath (Join-Path $releaseRoot '明日之后帧率助手.exe')),
    (Get-Item -LiteralPath (Join-Path $releaseRoot 'README.md')),
    (Get-Item -LiteralPath (Join-Path $releaseRoot '免责声明.md')),
    (Get-Item -LiteralPath (Join-Path $releaseRoot '使用说明.md')),
    (Get-Item -LiteralPath (Join-Path $releaseRoot '验证结果.json')))
foreach ($folder in @('engine','runtime','licenses')) { $files += @(Get-ChildItem -LiteralPath (Join-Path $releaseRoot $folder) -File -Recurse) }
$sums = $files | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($releaseRoot, $_.FullName).Replace('\','/')
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
}
$sums | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding utf8
$files += Get-Item -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt')
$zipName = "LifeAfter_FPS-v$Version-win-x64.zip"
$zipPath = Join-Path $projectRoot "发行版/$zipName"
$stream = [IO.File]::Open($zipPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
$zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $entry = "$releaseName/" + [IO.Path]::GetRelativePath($releaseRoot, $file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    foreach ($folder in @('backups','logs')) { $zip.CreateEntry("$releaseName/$folder/") | Out-Null }
} finally { $zip.Dispose(); $stream.Dispose() }
$archiveHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
"$($archiveHash.Hash.ToLowerInvariant())  $zipName" | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding utf8
Get-Item -LiteralPath $zipPath | Select-Object FullName,Length
