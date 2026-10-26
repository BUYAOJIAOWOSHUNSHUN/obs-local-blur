// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Windows.Forms;
using Microsoft.Win32;
[assembly: AssemblyTitle("局部打码插件安装程序")]
[assembly: AssemblyDescription("Composite Blur 1.6.1 矩形与椭圆九点编辑版 r5")]
[assembly: AssemblyCompany("不要叫我顺顺 / upstream FiniteSingularity")]
[assembly: AssemblyVersion("1.6.1.0")]
[assembly: AssemblyFileVersion("1.6.1.0")]

internal static class Installer
{
    [StructLayout(LayoutKind.Sequential)] struct FileInfoNative { public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Creation, Access, Write; public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow; }
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInfoNative info);
    internal static string Hash(byte[] data) { using (var h = SHA256.Create()) return BitConverter.ToString(h.ComputeHash(data)).Replace("-", "").ToLowerInvariant(); }
    internal static bool Allowed(string path)
    {
        if (String.IsNullOrEmpty(path) || path.Contains('\\') || path.Contains(':') || path.StartsWith("/") || path.Split('/').Any(p => p == ".." || p == "." || p.Length == 0)) return false;
        return path == "obs-plugins/64bit/obs-composite-blur.dll" || path.StartsWith("data/obs-plugins/obs-composite-blur/", StringComparison.Ordinal);
    }
    internal static Dictionary<string, byte[]> LoadPayload()
    {
        var assembly = Assembly.GetExecutingAssembly();
        var hashes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        using (var reader = new StreamReader(assembly.GetManifestResourceStream("payload.sha256"))) {
            string line; while ((line = reader.ReadLine()) != null) {
                if (line.Length < 67 || line.Substring(64, 2) != "  " || !Regex.IsMatch(line.Substring(0, 64), "^[a-f0-9]{64}$")) throw new InvalidDataException("安装清单格式错误。");
                var path = line.Substring(66); if (!Allowed(path) || hashes.ContainsKey(path)) throw new InvalidDataException("安装清单路径错误。");
                hashes.Add(path, line.Substring(0, 64));
            }
        }
        var files = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase); long total = 0;
        using (var stream = assembly.GetManifestResourceStream("payload.zip"))
        using (var zip = new ZipArchive(stream, ZipArchiveMode.Read)) {
            foreach (var entry in zip.Entries) {
                if (entry.Name.Length == 0) continue;
                if (!Allowed(entry.FullName) || !hashes.ContainsKey(entry.FullName) || files.ContainsKey(entry.FullName) || entry.Length > 10 * 1024 * 1024 || (total += entry.Length) > 100 * 1024 * 1024 || files.Count >= 100) throw new InvalidDataException("安装包范围或大小异常。");
                using (var input = entry.Open()) using (var buffer = new MemoryStream()) {
                    byte[] block = new byte[8192]; int n; long read = 0;
                    while ((n = input.Read(block, 0, block.Length)) > 0) { read += n; if (read > entry.Length) throw new InvalidDataException("安装文件大小异常。"); buffer.Write(block, 0, n); }
                    byte[] bytes = buffer.ToArray(); if (read != entry.Length || Hash(bytes) != hashes[entry.FullName]) throw new InvalidDataException("安装文件校验失败。");
                    files.Add(entry.FullName, bytes);
                }
            }
        }
        if (files.Count != hashes.Count || !files.ContainsKey("obs-plugins/64bit/obs-composite-blur.dll")) throw new InvalidDataException("安装包文件不完整。");
        return files;
    }
    internal static void NoLinks(string path)
    {
        for (var p = new DirectoryInfo(Path.GetFullPath(path)); p != null; p = p.Parent) {
            if ((Directory.Exists(p.FullName) || File.Exists(p.FullName)) && (File.GetAttributes(p.FullName) & FileAttributes.ReparsePoint) != 0) throw new IOException("目标路径含目录链接，请选择实际 OBS 安装目录。");
        }
        if (File.Exists(path)) using (var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read)) {
            FileInfoNative info; if (!GetFileInformationByHandle(file.SafeFileHandle, out info) || info.Links != 1) throw new IOException("目标文件为硬链接或无法验证，请使用普通 OBS 安装目录。");
        }
    }
    internal static void SafeWrite(string path, byte[] data)
    {
        NoLinks(path); string temporary = Path.Combine(Path.GetDirectoryName(path), ".obs-local-blur-" + Guid.NewGuid().ToString("N") + ".tmp");
        try {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { stream.Write(data, 0, data.Length); stream.Flush(true); }
            NoLinks(path);
            if (File.Exists(path)) File.Replace(temporary, path, null); else File.Move(temporary, path);
        } finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    internal static string Target(string root, string relative)
    {
        if (!Allowed(relative)) throw new InvalidDataException("不允许的安装路径。");
        string full = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!full.StartsWith(Path.GetFullPath(root).TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("路径越界。");
        NoLinks(full); return full;
    }
    internal static void RequireStopped()
    {
        var processes = Process.GetProcessesByName("obs64"); try { if (processes.Length != 0) throw new IOException("请保存工作并正常退出 OBS，再点击安装。安装程序不会强制关闭 OBS。"); } finally { foreach (var p in processes) p.Dispose(); }
    }
    internal static void ValidateObs(string root)
    {
        NoLinks(root);
        string exe = Path.Combine(root, "bin", "64bit", "obs64.exe");
        if (!File.Exists(exe)) throw new IOException("请选择包含 bin\\64bit\\obs64.exe 的 OBS 安装根目录。");
        var version = FileVersionInfo.GetVersionInfo(exe);
        if (version.FileMajorPart != 32 || version.FileMinorPart != 2 || version.FileBuildPart != 2) throw new IOException("本版仅针对 OBS 32.2.2 验证。检测到版本：" + version.FileVersion);
        RequireStopped();
    }
    // Transaction core shared with offline dummy-directory checks. Only the
    // interactive entry point supplies ValidateObs/RequireStopped.
    internal static string InstallFiles(string root, Dictionary<string, byte[]> files, Action guard, Action<int> afterWrite)
    {
        root = Path.GetFullPath(root); NoLinks(root); guard();
        foreach (var name in files.Keys) Target(root, name);
        var backup = Path.Combine(root, "obs-local-blur-backups", DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
        NoLinks(backup); Directory.CreateDirectory(backup);
        var previous = new Dictionary<string, string>(); var touched = new List<string>();
        // Finish every backup before replacing the first file.
        foreach (var name in files.Keys) {
            string dest = Target(root, name);
            if (File.Exists(dest)) {
                string saved = Path.Combine(backup, name.Replace('/', '\\')); Directory.CreateDirectory(Path.GetDirectoryName(saved));
                File.Copy(dest, saved, false);
                if (Hash(File.ReadAllBytes(saved)) != Hash(File.ReadAllBytes(dest))) throw new IOException("备份校验失败，未开始替换。");
                previous.Add(name, saved);
            }
        }
        File.WriteAllLines(Path.Combine(backup, "files.txt"), files.Keys.Select(p => (previous.ContainsKey(p) ? "restore  " : "new  ") + p), Encoding.UTF8);
        try {
            foreach (var item in files) {
                guard(); string dest = Target(root, item.Key); Directory.CreateDirectory(Path.GetDirectoryName(dest)); touched.Add(item.Key);
                SafeWrite(dest, item.Value);
                if (Hash(File.ReadAllBytes(dest)) != Hash(item.Value)) throw new IOException("安装文件校验失败。");
                afterWrite(touched.Count);
            }
        } catch (Exception original) {
            var failures = new List<string>();
            foreach (var name in touched.AsEnumerable().Reverse()) {
                try { string dest = Target(root, name); if (previous.ContainsKey(name)) SafeWrite(dest, File.ReadAllBytes(previous[name])); else if (File.Exists(dest)) File.Delete(dest); }
                catch { failures.Add(name); }
            }
            if (failures.Count > 0) throw new IOException("安装失败，部分文件无法自动回退。请退出 OBS 后从此目录恢复：" + backup + "\n" + String.Join("\n", failures), original);
            throw new IOException("安装失败，已恢复本次替换前的文件。备份：" + backup + "\n" + original.Message, original);
        }
        return backup;
    }
    internal static string DetectObs()
    {
        var candidates = new List<string>();
        foreach (var hive in new[] { RegistryHive.LocalMachine, RegistryHive.CurrentUser }) foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 }) {
            try { using (var root = RegistryKey.OpenBaseKey(hive, view)) {
                using (var key = root.OpenSubKey(@"SOFTWARE\OBS Studio")) { if (key != null) { var value = key.GetValue("") as string; if (value != null) candidates.Add(value); } }
                using (var key = root.OpenSubKey(@"SOFTWARE\Valve\Steam")) { if (key != null) foreach (var name in new[] { "InstallPath", "SteamPath" }) {
                    var steam = key.GetValue(name) as string; if (String.IsNullOrEmpty(steam)) continue;
                    candidates.Add(Path.Combine(steam, "steamapps", "common", "OBS Studio"));
                    var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                    if (File.Exists(vdf) && new FileInfo(vdf).Length < 1024 * 1024) foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\"")) candidates.Add(Path.Combine(m.Groups[1].Value.Replace("\\\\", "\\"), "steamapps", "common", "OBS Studio"));
                } }
            } } catch { }
        }
        candidates.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "obs-studio"));
        return candidates.FirstOrDefault(p => File.Exists(Path.Combine(p, "bin", "64bit", "obs64.exe"))) ?? "";
    }
    [STAThread] private static void Main()
    {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        try { Application.Run(new SetupForm()); } catch (Exception e) { MessageBox.Show(e.Message, "局部打码安装失败", MessageBoxButtons.OK, MessageBoxIcon.Error); }
    }
}

internal sealed class SetupForm : Form
{
    readonly bool previewMode;
    protected override bool ShowWithoutActivation { get { return previewMode; } }
    readonly TextBox folder = new TextBox(); readonly Button install = new Button(); readonly Label status = new Label();
    readonly CheckBox consent = new CheckBox(); readonly Dictionary<string, byte[]> payload;
    public SetupForm(string exampleDirectory = null)
    {
        previewMode = exampleDirectory != null;
        Text = "OBS局部打码插件 · 安装 / 更新"; Font = new Font("Microsoft YaHei UI", 10); ClientSize = new Size(650, 470);
        FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen;
        var title = new Label { Text = "局部打码 1.6.1 r5 · 矩形与椭圆可视编辑", AutoSize = true, Location = new Point(22, 20), Font = new Font(Font.FontFamily, 15, FontStyle.Bold) }; Controls.Add(title);
        Controls.Add(new Label { Text = "版本功能优化：不要叫我顺顺\n基于 FiniteSingularity 的 Composite Blur 1.6.1 · GPL-2.0\n适用于 Windows x64 / OBS 32.2.2", Location = new Point(24, 65), Size = new Size(600, 78) });
        Controls.Add(new Label { Text = "OBS 安装目录（不是 obs-plugins 子目录）", Location = new Point(24, 150), AutoSize = true });
        folder.SetBounds(24, 180, 500, 30); folder.Text = exampleDirectory ?? Installer.DetectObs(); Controls.Add(folder);
        var browse = new Button { Text = "浏览…", Location = new Point(535, 178), Size = new Size(88, 33) };
        browse.Click += delegate { using (var dialog = new FolderBrowserDialog { Description = "选择 OBS 安装根目录", ShowNewFolderButton = false }) { if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath; } }; Controls.Add(browse);
        var license = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical, Location = new Point(24, 225), Size = new Size(599, 95) };
        using (var reader = new StreamReader(Assembly.GetExecutingAssembly().GetManifestResourceStream("LICENSE"))) license.Text = reader.ReadToEnd(); Controls.Add(license);
        consent.Text = "我已阅读许可，保存工作并退出 OBS"; consent.SetBounds(24, 328, 580, 28); consent.CheckedChanged += delegate { install.Enabled = consent.Checked; }; Controls.Add(consent);
        status.Text = "安装前自动备份原插件；不修改场景、录制或账号配置。"; status.SetBounds(24, 365, 599, 42); Controls.Add(status);
        install.Text = "安装 / 更新"; install.SetBounds(388, 420, 125, 34); install.Enabled = false; install.Click += Install; Controls.Add(install);
        var close = new Button { Text = "关闭", Location = new Point(531, 420), Size = new Size(92, 34) }; close.Click += delegate { Close(); }; Controls.Add(close);
        payload = Installer.LoadPayload();
    }
    void Install(object sender, EventArgs args)
    {
        install.Enabled = false;
        try {
            var root = folder.Text.Trim(); Installer.ValidateObs(root); status.Text = "正在备份、安装并校验…"; status.Refresh();
            var backup = Installer.InstallFiles(root, payload, Installer.RequireStopped, delegate(int n) { });
            status.Text = "安装完成。可以打开 OBS，添加或查看“局部打码”滤镜。";
            MessageBox.Show(this, "安装完成。\n\n原插件备份：\n" + backup + "\n\n需要回退时，退出 OBS 后将备份内对应文件复制回原位置。", "局部打码", MessageBoxButtons.OK, MessageBoxIcon.Information);
        } catch (Exception e) { status.Text = "未完成安装。"; MessageBox.Show(this, e.Message, "安装提示", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        finally { install.Enabled = consent.Checked; }
    }
}
