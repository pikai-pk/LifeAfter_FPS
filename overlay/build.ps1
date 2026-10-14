param([switch]$SmokeTest, [string]$OutputDirectory, [string]$PresentMonExecutable, [switch]$RenderTarget)
$ErrorActionPreference = 'Stop'
$fpsProject = $PSScriptRoot
$fpsOutput = Join-Path $fpsProject '程序'
if ($SmokeTest) { $fpsOutput = Join-Path $fpsProject 'tests/live-test' }
if ($OutputDirectory) { $fpsOutput = [IO.Path]::GetFullPath($OutputDirectory) }
New-Item -ItemType Directory -Path $fpsOutput -Force | Out-Null
$fpsVs = & 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $fpsVs) { throw '未找到 MSVC C++ 编译工具' }
$fpsToolRoot = (Get-ChildItem -LiteralPath (Join-Path $fpsVs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$fpsSdk = 'C:/Program Files (x86)/Windows Kits/10'
$fpsSdkVersion = (Get-ChildItem -LiteralPath (Join-Path $fpsSdk 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$fpsCompiler = Join-Path $fpsToolRoot 'bin/Hostx64/x64/cl.exe'
$fpsSource = Join-Path $fpsProject 'src/main.cpp'
$fpsExe = Join-Path $fpsOutput 'FPS悬浮窗.exe'
$fpsObject = Join-Path $fpsOutput 'main.obj'
if ($RenderTarget) { $fpsSource = Join-Path $fpsProject 'tests/render-target.cpp'; $fpsExe = Join-Path $fpsOutput 'FpsTestRenderer.exe'; $fpsObject = Join-Path $fpsOutput 'render-target.obj' }
$fpsIncludeArgs = @('/I' + (Join-Path $fpsToolRoot 'include'))
foreach ($fpsHeader in @('ucrt','shared','um','winrt')) { $fpsIncludeArgs += '/I' + (Join-Path $fpsSdk "Include/$fpsSdkVersion/$fpsHeader") }
$fpsLibArgs = @()
foreach ($fpsLibrary in @((Join-Path $fpsToolRoot 'lib/x64'), (Join-Path $fpsSdk "Lib/$fpsSdkVersion/ucrt/x64"), (Join-Path $fpsSdk "Lib/$fpsSdkVersion/um/x64"))) { $fpsLibArgs += '/LIBPATH:' + $fpsLibrary }
$fpsDefines = @('/DWIN32_LEAN_AND_MEAN')
if ($SmokeTest) { $fpsDefines += '/DFPS_SMOKE_TEST' }
& $fpsCompiler /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 @fpsDefines @fpsIncludeArgs $fpsSource "/Fo$fpsObject" "/Fe$fpsExe" /link /SUBSYSTEM:WINDOWS @fpsLibArgs user32.lib gdi32.lib gdiplus.lib shell32.lib
if ($LASTEXITCODE -ne 0) { throw '编译失败' }
if ($RenderTarget) { return }
if (!$PresentMonExecutable) {
    $bundled = Join-Path $fpsProject '程序/PresentMon.exe'
    if (Test-Path -LiteralPath $bundled) { $PresentMonExecutable = $bundled }
    else { $PresentMonExecutable = Join-Path $fpsProject '../../../setup/analysis-tools/presentmon/PresentMon-2.6.0-x64.exe' }
}
if (!(Test-Path -LiteralPath $PresentMonExecutable)) { throw '请用 -PresentMonExecutable 指定官方 PresentMon 2.6.0 x64。' }
$fpsCollectorDestination = Join-Path $fpsOutput 'PresentMon.exe'
if ([IO.Path]::GetFullPath($PresentMonExecutable) -ne [IO.Path]::GetFullPath($fpsCollectorDestination)) { Copy-Item -LiteralPath $PresentMonExecutable -Destination $fpsCollectorDestination -Force }
