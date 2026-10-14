param([string]$Version = '1.0.1')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw '版本格式必须是 x.y.z' }
$projectRoot = $PSScriptRoot
$releaseRoot = Join-Path $projectRoot "发行版/明日之后帧率助手-v$Version-win-x64"
if (!(Test-Path -LiteralPath (Join-Path $projectRoot '程序/runtime/node.exe'))) { & (Join-Path $projectRoot 'bootstrap-runtime.ps1') }
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
& dotnet publish (Join-Path $projectRoot 'src/FrameUnlocker.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true "-p:Version=$Version" -o $releaseRoot
if ($LASTEXITCODE -ne 0) { throw '发布编译失败' }
foreach ($folder in @('engine','runtime','licenses')) {
    $destination = Join-Path $releaseRoot $folder
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot "程序/$folder") -Destination $releaseRoot -Recurse -Force
}
foreach ($folder in @('backups','logs')) { New-Item -ItemType Directory -Path (Join-Path $releaseRoot $folder) -Force | Out-Null }
Copy-Item -LiteralPath (Join-Path $projectRoot '使用说明.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '免责声明.md') -Destination $releaseRoot -Force
Write-Output $releaseRoot
