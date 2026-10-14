param([string]$NodeExecutable)
$ErrorActionPreference = 'Stop'
$runtimeFolder = Join-Path $PSScriptRoot '程序/runtime'
$target = Join-Path $runtimeFolder 'node.exe'
$expectedHash = '9a4eb5f1c29c6a2e93852ead46b999e284a6a5ca8bab4d4e241d587d025a52de'
New-Item -ItemType Directory -Path $runtimeFolder -Force | Out-Null
function Test-NodeHash([string]$File) {
    return (Test-Path -LiteralPath $File -PathType Leaf) -and (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash
}
if (Test-NodeHash $target) { Write-Output 'Node.js v24.18.0 x64 已就绪'; return }
$temporary = Join-Path $runtimeFolder ('node-' + [Guid]::NewGuid().ToString('N') + '.tmp')
try {
    if ($NodeExecutable) { Copy-Item -LiteralPath $NodeExecutable -Destination $temporary }
    else { Invoke-WebRequest -Uri 'https://nodejs.org/dist/v24.18.0/win-x64/node.exe' -OutFile $temporary }
    if (!(Test-NodeHash $temporary)) { throw 'Node.js 文件摘要不匹配，需要官方 v24.18.0 Windows x64 版本' }
    Move-Item -LiteralPath $temporary -Destination $target -Force
    Write-Output 'Node.js v24.18.0 x64 校验通过'
} finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
