# 通用 FPS 悬浮窗 v1.0.0

作者 **pk**。Windows x64 原生 C++17 / Win32 / GDI+ 程序，采用深色半透明底板和浅色 FPS 数字，显示在目标进程窗口所在显示器的右上角，鼠标可以穿透悬浮窗。

## 使用

1. 完整解压 `PK_FPS_Overlay-v1.0.0-win-x64.zip`，保留同目录的 `PresentMon.exe`、`fps-overlay.ini` 和 licenses。
2. 双击 `FPS悬浮窗.exe`，在下拉列表中选择运行中的目标进程。列表包含进程名、PID 和可用的窗口标题；同名进程可以分别选择。
3. 点击“开始检测”。新启动的进程请先点击“刷新进程”；切换目标时再次选择并点击开始检测。
4. 控制窗口显示目标 PID、采样状态和帧率；悬浮窗显示最近约 1 秒的呈现 FPS，每 500 毫秒刷新。

默认不选择游戏，不限定于明日之后。进程必须产生 PresentMon 能识别的图形呈现事件；普通后台服务、无图形呈现的进程和部分不兼容渲染路径可能没有 FPS 数据，此时显示等待状态。

目标退出后需重新选择。程序按 PID 和进程创建时间检查身份，避免 PID 被复用后采集其他进程。

## 控制与退出

- 关闭控制窗口后，程序继续在托盘运行。
- 双击托盘图标，或右键选择“选择检测进程”，可重新打开控制窗口。
- 托盘菜单支持显示/隐藏、重新采样、状态和退出。
- `Ctrl+Alt+F11` 显示/隐藏；`Ctrl+Alt+F12` 退出。若热键被其他程序占用，请使用托盘菜单或控制窗口按钮。
- `fps-overlay.ini` 中 `style=dark_panel` 为默认深色底板；`dark_text` 为透明背景深色文字，重启生效。

## 采样原理和限制

本程序通过 [PresentMon 2.6.0](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0) 的 ETW 采样器按具体 `--process_id` 收集呈现事件。由 `MsBetweenPresents` 计算 `1000 × 有效样本数 / 有效间隔总毫秒数`；多个交换链时选择近期样本最多且数据新鲜的链。字段和参数见 [PresentMon 控制台文档](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md)。

显示的是进程的呈现频率，实际显示到屏幕的帧率可能不同。窗口每半秒刷新和目标应用的 FPS 是不同概念。独占全屏可能遮住桌面悬浮窗，窗口化或无边框模式更适合。

采集权限不足时，可自行右键选择“以管理员身份运行”。本工具不自动提权，不修改游戏包，不注入进程，不采集键鼠输入事件；正常运行不保存逐帧 CSV，也不发送采样结果到网络。

本软件为非官方个人工具，按现状提供，不保证所有应用、渲染路径或系统都可采样。请自行判断使用条件和风险；适用法律规定不能免除的责任仍依法承担。

## 构建和验证

需要 Visual Studio C++ x64 编译工具、Windows SDK 和 PowerShell 7。用 `bootstrap-presentmon.ps1` 下载并校验固定版本的官方 PresentMon，或通过 `build.ps1 -PresentMonExecutable <本地文件>` 提供它。

```powershell
./bootstrap-presentmon.ps1
./build.ps1 -OutputDirectory './发行版/PK_FPS_Overlay-v1.0.0-win-x64'
./tests/verify-generic.ps1 -ProgramDirectory './发行版/PK_FPS_Overlay-v1.0.0-win-x64'
./package-generic.ps1
```

验证覆盖 CSV 引号、60/144/300 FPS、数据过期、非法数值、PID 筛选、多交换链、PID 身份复用保护及非编辑型下拉列表，并启动独立 D3D11 测试渲染进程进行实际采样。测试窗口会短暂显示，完成后关闭。源文件 `tests/render-target.cpp` 仅用于测试。

`--self-test`、`--verify-picker` 输出隔离的验证文件。`--diagnostics --capture-seconds 12 --process-id <PID>` 用于指定 PID 的短时采样验证，普通启动不写诊断文件。

发行包附 `验证结果.json`、`SHA256SUMS.txt` 和 PresentMon 的许可证。源码本身未单独指定开源许可证。
