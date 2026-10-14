using System.Diagnostics;
using System.Text;
using System.Text.Json;
using K4os.Compression.LZ4;

namespace FrameUnlocker;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--lz4")
        {
            try
            {
                using var memory = new MemoryStream(); using var input = Console.OpenStandardInput();
                var scratch = new byte[8192]; int read;
                while ((read = input.Read(scratch)) != 0) { memory.Write(scratch, 0, read); if (memory.Length > 1024 * 1024) throw new InvalidDataException("Compression input too large"); }
                var bytes = memory.ToArray(); var encoded = new byte[LZ4Codec.MaximumOutputSize(bytes.Length)];
                var count = LZ4Codec.Encode(bytes, 0, bytes.Length, encoded, 0, encoded.Length, LZ4Level.L12_MAX);
                if (count <= 0) throw new InvalidDataException("Compression failed");
                var verify = new byte[bytes.Length];
                if (LZ4Codec.Decode(encoded, 0, count, verify, 0, verify.Length) != bytes.Length || !verify.AsSpan().SequenceEqual(bytes)) throw new InvalidDataException("Compression verification failed");
                using var output = Console.OpenStandardOutput(); output.Write(encoded, 0, count); return 0;
            }
            catch (Exception e) { using var error = new StreamWriter(Console.OpenStandardError()); error.Write(e.Message); return 1; }
        }
        ApplicationConfiguration.Initialize();
        if (args.Length == 2 && args[0] == "--verify-agreement")
        {
            File.WriteAllText(args[1], JsonSerializer.Serialize(UserAgreement.Verify(), new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        if (args.Length == 2 && args[0] == "--verify-ui")
        {
            using var form = new MainForm(loadPreferences: false);
            File.WriteAllText(args[1], JsonSerializer.Serialize(form.VerifyPresetRouting(), new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, e) => MessageBox.Show(e.Exception.Message, "操作未完成", MessageBoxButtons.OK, MessageBoxIcon.Information);
        if (!UserAgreement.EnsureAccepted()) return 0;
        Application.Run(new MainForm());
        return 0;
    }
}

internal sealed class MainForm : Form
{
    readonly TextBox game = new() { Text = @"D:\Software\Games\LifeAfter", Dock = DockStyle.Fill };
    const int DefaultFps = 120;
    static readonly int[] Presets = [120, 144, 165, 180, 240, 300, 360, 480, 600, 800, 1000];
    readonly ComboBox fps = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 220 };
    readonly Label status = new() { Text = "先检查游戏包，确认当前版本支持独立补丁。", AutoSize = true };
    readonly TextBox details = new() { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Color.FromArgb(245, 247, 250), Font = new Font("Microsoft YaHei UI", 10) };
    readonly Button check = new() { Text = "检查版本", AutoSize = true };
    readonly Button plan = new() { Text = "预览补丁", AutoSize = true };
    readonly Button apply = new() { Text = "备份并应用", AutoSize = true };
    readonly Button restore = new() { Text = "恢复备份", AutoSize = true };
    readonly Button browse = new() { Text = "选择目录", AutoSize = true };
    readonly Button openBackups = new() { Text = "打开备份目录", AutoSize = true };
    readonly Button agreement = new() { Text = "使用协议", AutoSize = true };
    static readonly string settingsPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LifeAfterFrameUnlocker", "settings.json");
    readonly string home = AppContext.BaseDirectory;
    bool busy;

    public MainForm(bool loadPreferences = true)
    {
        Text = "明日之后帧率助手 · v1.0.1";
        ClientSize = new Size(920, 660);
        MinimumSize = new Size(780, 560);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Microsoft YaHei UI", 10);
        BackColor = Color.FromArgb(248, 250, 253);
        foreach (var value in Presets) fps.Items.Add(new FpsPreset(value));
        fps.SelectedIndex = Array.IndexOf(Presets, DefaultFps);
        if (loadPreferences) LoadSettings();
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), ColumnCount = 1, RowCount = 7 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(new Label { Text = "明日之后 / 帧率助手", AutoSize = true, Font = new Font(Font.FontFamily, 22, FontStyle.Bold), Margin = new Padding(0, 0, 0, 16) });
        var pathRow = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 3, Margin = new Padding(0, 0, 0, 14) };
        pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize)); pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        pathRow.Controls.Add(new Label { Text = "游戏目录", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0); pathRow.Controls.Add(game, 1, 0); pathRow.Controls.Add(browse, 2, 0); layout.Controls.Add(pathRow);
        var fpsRow = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, Margin = new Padding(0, 0, 0, 14) };
        fpsRow.Controls.Add(new Label { Text = "帧率档位", AutoSize = true, Margin = new Padding(0, 5, 12, 0) }); fpsRow.Controls.Add(fps);
        fpsRow.Controls.Add(new Label { Text = "选择上限后，预览补丁 → 备份并应用", AutoSize = true, Margin = new Padding(10, 5, 0, 0) }); layout.Controls.Add(fpsRow);
        var actions = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Top, Margin = new Padding(0, 0, 0, 14) };
        foreach (var button in new[] { check, plan, apply, restore, openBackups, agreement }) { button.Padding = new Padding(10, 7, 10, 7); button.Margin = new Padding(0, 0, 10, 0); actions.Controls.Add(button); }
        apply.BackColor = Color.FromArgb(39, 91, 196); apply.ForeColor = Color.White; apply.FlatStyle = FlatStyle.Flat;
        layout.Controls.Add(actions); status.Margin = new Padding(0, 0, 0, 12); layout.Controls.Add(status); layout.Controls.Add(details);
        layout.Controls.Add(new Label { Text = "先退出游戏及更新器。实际 FPS 取决于硬件与同步设置；包体不匹配时停止写入。", AutoSize = true, Margin = new Padding(0, 12, 0, 0) });
        Controls.Add(layout);
        browse.Click += (_, _) => { using var d = new FolderBrowserDialog { Description = "选择明日之后安装目录", UseDescriptionForTitle = true }; if (d.ShowDialog(this) == DialogResult.OK) game.Text = d.SelectedPath; };
        check.Click += async (_, _) => await Run("check");
        plan.Click += async (_, _) => await Run("plan");
        apply.Click += async (_, _) => await Run("apply");
        agreement.Click += (_, _) => { using var dialog = new AgreementForm(); dialog.ShowDialog(this); };
        openBackups.Click += (_, _) => {
            try { var folder = Path.Combine(home, "backups"); Directory.CreateDirectory(folder); Process.Start(new ProcessStartInfo(folder) { UseShellExecute = true }); }
            catch (Exception e) { MessageBox.Show(this, e.Message, "无法打开备份目录"); }
        };
        restore.Click += async (_, _) => {
            using var d = new OpenFileDialog { Title = "选择本程序的备份 manifest.json", Filter = "备份清单|manifest.json", InitialDirectory = Path.Combine(home, "backups") };
            if (d.ShowDialog(this) == DialogResult.OK) await Run("restore", d.FileName);
        };
        FormClosing += (_, e) => { if (busy) { e.Cancel = true; status.Text = "操作进行中，请等待写入或回滚结束。"; } else SaveSettings(); };
        details.Text = "1. 选择游戏安装目录，点击“检查版本”。\r\n2. 从下拉列表选择帧率档位，点击“预览补丁”。\r\n3. 退出游戏及更新器后，点击“备份并应用”。\r\n4. 应用完成后再次检查，再启动游戏查看实际 FPS。\r\n\r\n每次写入都会保留独立备份；需要还原时选择对应备份目录中的 manifest.json。\r\n\r\n本版本只支持已验证的游戏脚本包。游戏更新后，请重新检查版本。";
    }

    sealed record FpsPreset(int Value)
    {
        public override string ToString() => Value == DefaultFps ? $"{DefaultFps} FPS（默认）" : $"{Value} FPS";
    }
    sealed record Preferences(string GameDirectory, int Fps, int DefaultFps = 0);
    int SelectedFps => fps.SelectedItem is FpsPreset selected ? selected.Value : throw new InvalidOperationException("请选择帧率档位。");
    void LoadSettings()
    {
        try {
            if (!File.Exists(settingsPath)) return;
            var saved = JsonSerializer.Deserialize<Preferences>(File.ReadAllText(settingsPath));
            if (saved == null) return;
            if (!string.IsNullOrWhiteSpace(saved.GameDirectory)) game.Text = saved.GameDirectory;
            // Adopt the new default once when upgrading from preferences written with the old default.
            var index = Array.IndexOf(Presets, saved.Fps); if (saved.DefaultFps == DefaultFps && index >= 0) fps.SelectedIndex = index;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { /* A damaged preference file must not prevent startup. */ }
    }
    void SaveSettings()
    {
        try {
            Directory.CreateDirectory(Path.GetDirectoryName(settingsPath)!);
            var temp = settingsPath + "." + Guid.NewGuid().ToString("N") + ".tmp";
            File.WriteAllText(temp, JsonSerializer.Serialize(new Preferences(game.Text.Trim(), SelectedFps, DefaultFps)));
            File.Move(temp, settingsPath, true);
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            status.Text = "档位和目录未能保存；当前操作仍可继续。";
        }
    }
    string[] EngineArguments(string mode, string? backup)
    {
        var values = new List<string> { Path.Combine(home, "engine", "cli.mjs"), "--mode", mode, "--game", game.Text.Trim(), "--fps", SelectedFps.ToString(System.Globalization.CultureInfo.InvariantCulture) };
        if (backup != null) { values.Add("--backup"); values.Add(backup); }
        return values.ToArray();
    }
    internal object VerifyPresetRouting()
    {
        if (SelectedFps != DefaultFps) throw new InvalidOperationException("Default preset is incorrect.");
        if (fps.DropDownStyle != ComboBoxStyle.DropDownList) throw new InvalidOperationException("The preset control allows manual input.");
        var rows = new List<object>();
        var previous = fps.SelectedIndex;
        for (var index = 0; index < Presets.Length; index++) {
            fps.SelectedIndex = index;
            foreach (var mode in new[] { "check", "plan", "apply", "restore" }) {
                var arguments = EngineArguments(mode, mode == "restore" ? "test manifest.json" : null);
                if (arguments[5] != "--fps" || arguments[6] != Presets[index].ToString() || arguments[2] != mode) throw new InvalidOperationException("Preset routing failed.");
                if (mode == "restore" && arguments[^1] != "test manifest.json") throw new InvalidOperationException("Backup routing failed.");
            }
            rows.Add(new { fps = SelectedFps, label = fps.SelectedItem!.ToString() });
        }
        fps.SelectedIndex = previous;
        return new { passed = true, editable = false, presets = rows, selected = SelectedFps };
    }

    async Task Run(string mode, string? backup = null)
    {
        if (busy) return;
        busy = true;
        foreach (var c in new Control[] { check, plan, apply, restore, browse, game, fps }) c.Enabled = false;
        status.Text = mode switch { "apply" => "正在校验、备份、应用并重读检查…", "restore" => "正在校验并恢复备份…", _ => "正在检查游戏包…" };
        details.Clear();
        try
        {
            SaveSettings();
            var node = Path.Combine(home, "runtime", "node.exe"); var script = Path.Combine(home, "engine", "cli.mjs");
            if (!File.Exists(node) || !File.Exists(script)) throw new InvalidOperationException("程序文件不完整，请完整解压整个程序目录。");
            var info = new ProcessStartInfo(node) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8, WorkingDirectory = home };
            foreach (var arg in EngineArguments(mode, backup)) info.ArgumentList.Add(arg);
            using var process = Process.Start(info) ?? throw new InvalidOperationException("分析引擎启动失败。");
            var stdout = process.StandardOutput.ReadToEndAsync(); var stderr = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            var text = await stdout; var error = await stderr;
            if (string.IsNullOrWhiteSpace(text)) throw new InvalidOperationException(string.IsNullOrWhiteSpace(error) ? "引擎没有返回结果，请检查程序目录写入权限。" : error);
            using var result = JsonDocument.Parse(text);
            var root = result.RootElement;
            status.Text = root.TryGetProperty("message", out var message) ? message.GetString() : "操作完成";
            details.Text = root.TryGetProperty("display", out var display) ? display.GetString() : text;
            if (root.TryGetProperty("reportPath", out var report)) details.AppendText(Environment.NewLine + Environment.NewLine + "检查报告：" + report.GetString());
            if (process.ExitCode != 0) status.ForeColor = Color.FromArgb(163, 49, 38); else status.ForeColor = Color.FromArgb(32, 116, 65);
        }
        catch (Exception e) { status.Text = "操作未完成"; status.ForeColor = Color.FromArgb(163, 49, 38); details.Text = e.Message; }
        finally { busy = false; foreach (var c in new Control[] { check, plan, apply, restore, browse, game, fps }) c.Enabled = true; }
    }
}
