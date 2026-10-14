param([string]$PresentMonExecutable)
$ErrorActionPreference = 'Stop'
$folder = Join-Path $PSScriptRoot '程序'
$target = Join-Path $folder 'PresentMon.exe'
$expectedHash = 'b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
function Test-CollectorHash([string]$File) { return (Test-Path -LiteralPath $File -PathType Leaf) -and (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash }
if (Test-CollectorHash $target) { Write-Output 'PresentMon 2.6.0 x64 已就绪'; return }
$temporary = Join-Path $folder ('presentmon-' + [Guid]::NewGuid().ToString('N') + '.tmp')
try {
    if ($PresentMonExecutable) { Copy-Item -LiteralPath $PresentMonExecutable -Destination $temporary }
    else { Invoke-WebRequest -Uri 'https://github.com/GameTechDev/PresentMon/releases/download/v2.6.0/PresentMon-2.6.0-x64.exe' -OutFile $temporary }
    if (!(Test-CollectorHash $temporary)) { throw 'PresentMon 文件摘要不匹配，需要官方 2.6.0 Windows x64 版本。' }
    Move-Item -LiteralPath $temporary -Destination $target -Force
    Write-Output 'PresentMon 2.6.0 x64 校验通过'
} finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
