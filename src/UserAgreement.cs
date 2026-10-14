using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace FrameUnlocker;

internal static class UserAgreement
{
    internal static readonly string Text = ReadText();
    internal static readonly string Hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Text)));
    static readonly string FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "LifeAfterFrameUnlocker", "agreement.json");
    sealed record Acceptance(string AgreementSha256, DateTimeOffset AcceptedAt);
    static string ReadText()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("FrameUnlocker.Disclaimer") ?? throw new InvalidOperationException("使用协议资源缺失。");
        using var reader = new StreamReader(stream, Encoding.UTF8);
        return reader.ReadToEnd();
    }
    internal static bool IsAccepted(string? path = null)
    {
        try {
            var accepted = JsonSerializer.Deserialize<Acceptance>(File.ReadAllText(path ?? FilePath));
            return accepted?.AgreementSha256 == Hash && accepted.AcceptedAt != default;
        } catch (Exception e) when (e is IOException or UnauthorizedAccessException or JsonException) { return false; }
    }
    internal static void Save(string? path = null)
    {
        var file = path ?? FilePath;
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        var temporary = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(new Acceptance(Hash, DateTimeOffset.UtcNow)));
        File.Move(temporary, file, true);
    }
    internal static bool EnsureAccepted()
    {
        if (IsAccepted()) return true;
        using var dialog = new AgreementForm();
        if (dialog.ShowDialog() != DialogResult.OK) return false;
        try { Save(); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
            MessageBox.Show("已确认协议，但无法保存记录；下次启动会再次提示。\n" + e.Message, "使用协议", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        return true;
    }
    internal static object Verify()
    {
        double elapsed = 0;
        using var dialog = new AgreementForm(() => TimeSpan.FromSeconds(elapsed));
        if (dialog.CanConfirm) throw new InvalidOperationException("Agreement is initially enabled.");
        dialog.SetAcknowledged(true);
        elapsed = 4.999; dialog.UpdateGate();
        if (dialog.CanConfirm) throw new InvalidOperationException("Agreement enabled before five seconds.");
        elapsed = 5; dialog.UpdateGate();
        if (!dialog.CanConfirm) throw new InvalidOperationException("Agreement did not enable after five seconds.");
        dialog.SetAcknowledged(false);
        if (dialog.CanConfirm) throw new InvalidOperationException("Agreement enabled without acknowledgement.");
        var folder = Path.Combine(Path.GetTempPath(), "LifeAfterAgreementTest-" + Guid.NewGuid().ToString("N"));
        var file = Path.Combine(folder, "agreement.json");
        try {
            if (IsAccepted(file)) throw new InvalidOperationException("Missing record accepted.");
            Save(file);
            if (!IsAccepted(file)) throw new InvalidOperationException("Acceptance did not persist.");
            File.WriteAllText(file, "{\"AgreementSha256\":\"outdated\",\"AcceptedAt\":\"2026-10-14T00:00:00Z\"}");
            if (IsAccepted(file)) throw new InvalidOperationException("Outdated agreement accepted.");
        } finally { if (File.Exists(file)) File.Delete(file); if (Directory.Exists(folder)) Directory.Delete(folder); }
        return new { passed = true, minimumReadSeconds = 5, requiresAcknowledgement = true, agreementSha256 = Hash, userAcceptanceModified = false };
    }
}

internal sealed class AgreementForm : Form
{
    readonly Stopwatch elapsed = new();
    readonly Func<TimeSpan> readingTime;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 100 };
    readonly CheckBox acknowledged = new() { Text = "我已阅读并理解以上说明，接受风险并继续", AutoSize = true };
    readonly Button confirm = new() { Text = "请阅读（5 秒）", Enabled = false, AutoSize = true, Padding = new Padding(14, 6, 14, 6) };
    internal bool CanConfirm => confirm.Enabled;
    internal AgreementForm(Func<TimeSpan>? clock = null)
    {
        readingTime = clock ?? (() => elapsed.Elapsed);
        Text = "使用协议与免责声明 · 作者 pk";
        ClientSize = new Size(760, 600); MinimumSize = new Size(650, 530);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Microsoft YaHei UI", 10);
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 1, RowCount = 4 };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize)); layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(new Label { Text = "非官方工具 · 使用前请阅读", AutoSize = true, Font = new Font(Font.FontFamily, 18, FontStyle.Bold), Margin = new Padding(0, 0, 0, 16) });
        layout.Controls.Add(new TextBox { Text = UserAgreement.Text, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill, BackColor = Color.FromArgb(248, 250, 253) });
        acknowledged.Margin = new Padding(0, 14, 0, 12); layout.Controls.Add(acknowledged);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true, FlowDirection = FlowDirection.RightToLeft };
        var decline = new Button { Text = "拒绝并退出", DialogResult = DialogResult.Cancel, AutoSize = true, Padding = new Padding(14, 6, 14, 6) };
        buttons.Controls.Add(confirm); buttons.Controls.Add(decline); layout.Controls.Add(buttons); Controls.Add(layout);
        CancelButton = decline;
        confirm.Click += (_, _) => { UpdateGate(); if (CanConfirm) DialogResult = DialogResult.OK; };
        acknowledged.CheckedChanged += (_, _) => UpdateGate();
        timer.Tick += (_, _) => UpdateGate();
        Shown += (_, _) => { elapsed.Restart(); timer.Start(); UpdateGate(); };
    }
    internal void SetAcknowledged(bool value) { acknowledged.Checked = value; UpdateGate(); }
    internal void UpdateGate()
    {
        var remaining = Math.Max(0, 5 - readingTime().TotalSeconds);
        confirm.Enabled = remaining <= 0 && acknowledged.Checked;
        confirm.Text = remaining > 0 ? $"请阅读（{Math.Ceiling(remaining):0} 秒）" : "确认并进入";
    }
    protected override void Dispose(bool disposing) { if (disposing) timer.Dispose(); base.Dispose(disposing); }
}
