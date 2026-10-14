param([string]$ProgramDirectory)
$ErrorActionPreference = 'Stop'
$name = 'PK_FPS_Overlay-v1.0.0-win-x64'
if (!$ProgramDirectory) { $ProgramDirectory = Join-Path $PSScriptRoot "发行版/$name" }
$program = (Resolve-Path -LiteralPath $ProgramDirectory).Path
$report = Get-Content -LiteralPath (Join-Path $program '验证结果.json') -Raw | ConvertFrom-Json
if (!$report.passed -or !$report.processPicker.passed -or !$report.liveNonGameProcess.valid -or !$report.liveNonGameProcess.selectedPidMatched) { throw '发布前必须通过通用进程实际采样验证。' }
foreach ($nameOfDoc in @('README.md','使用说明.md')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $program $nameOfDoc) -Force }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '程序/fps-overlay-generic.ini') -Destination (Join-Path $program 'fps-overlay.ini') -Force
New-Item -ItemType Directory -Path (Join-Path $program 'licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '程序/licenses/PresentMon-LICENSE.txt') -Destination (Join-Path $program 'licenses/PresentMon-LICENSE.txt') -Force
$files = @('FPS悬浮窗.exe','PresentMon.exe','fps-overlay.ini','licenses/PresentMon-LICENSE.txt','README.md','使用说明.md','验证结果.json')
$files | ForEach-Object { "$((Get-FileHash -LiteralPath (Join-Path $program $_) -Algorithm SHA256).Hash.ToLowerInvariant())  $_" } | Set-Content -LiteralPath (Join-Path $program 'SHA256SUMS.txt') -Encoding utf8
$files += 'SHA256SUMS.txt'
$outputFolder = Join-Path $PSScriptRoot '发行版'
New-Item -ItemType Directory -Path $outputFolder -Force | Out-Null
$zipPath = Join-Path $outputFolder ($name + '.zip')
$stream = [IO.File]::Open($zipPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
$zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try { foreach ($file in $files) { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $program $file),"$name/$file",[IO.Compression.CompressionLevel]::Optimal) | Out-Null } }
finally { $zip.Dispose(); $stream.Dispose() }
"$((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant())  $name.zip" | Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding utf8
Get-Item -LiteralPath $zipPath | Select-Object FullName,Length
