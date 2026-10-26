// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Windows.Forms;
internal static class InstallerTests
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool CreateHardLink(string path, string existing, IntPtr security);
    static int count;
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); count++; Console.WriteLine("PASS " + message); }
    [STAThread] static int Main(string[] args)
    {
        try {
            var root = Path.GetFullPath(args[0]); if (Directory.Exists(root)) throw new Exception("Test directory must be new"); Directory.CreateDirectory(root);
            var payload = Installer.LoadPayload(); Check(payload.Count > 30, "embedded payload hashes and allowlist");
            foreach (var p in new[] { "../escape", "C:/escape", "/escape", "data/obs-plugins/obs-composite-blur/../evil", "data\\obs-plugins\\evil", "obs-plugins/64bit/other.dll" }) Check(!Installer.Allowed(p), "reject unsafe or unrelated path");
            bool blocked = false; try { Installer.ValidateObs(root); } catch (IOException) { blocked = true; } Check(blocked, "reject non-OBS installation directory");
            string key = "obs-plugins/64bit/obs-composite-blur.dll", added = "data/obs-plugins/obs-composite-blur/locale/zh-CN.ini";
            var files = new Dictionary<string, byte[]> { { key, new byte[] { 5, 6, 7 } }, { added, new byte[] { 8, 9 } } };
            var dest = Installer.Target(root, key); Directory.CreateDirectory(Path.GetDirectoryName(dest)); File.WriteAllBytes(dest, new byte[] { 1, 2, 3 });
            var unrelated = Path.Combine(root, "personal-scene.json"); File.WriteAllText(unrelated, "dummy-scene-untouched");
            var backup = Installer.InstallFiles(root, files, delegate { }, delegate(int n) { });
            Check(File.ReadAllBytes(dest).SequenceEqual(files[key]), "install updates expected file");
            Check(File.ReadAllBytes(Path.Combine(backup, key.Replace('/', '\\'))).SequenceEqual(new byte[] { 1, 2, 3 }), "original file backed up exactly");
            Check(File.ReadAllText(unrelated) == "dummy-scene-untouched", "unrelated scene remains unchanged");
            var root2 = Path.Combine(root, "rollback"); Directory.CreateDirectory(root2);
            var dest2 = Installer.Target(root2, key); Directory.CreateDirectory(Path.GetDirectoryName(dest2)); File.WriteAllBytes(dest2, new byte[] { 1, 2, 3 });
            bool rolledBack = false;
            try { Installer.InstallFiles(root2, files, delegate { }, delegate(int n) { if (n == 2) throw new IOException("injected copy failure"); }); } catch (IOException) { rolledBack = true; }
            Check(rolledBack && File.ReadAllBytes(dest2).SequenceEqual(new byte[] { 1, 2, 3 }), "failed install restores original file");
            Check(!File.Exists(Installer.Target(root2, added)), "failed install removes only its newly created file");
            string outside = Path.Combine(root, "outside-sentinel.txt"), linked = Path.Combine(root, "hardlink.txt"); File.WriteAllText(outside, "sentinel");
            Check(CreateHardLink(linked, outside, IntPtr.Zero), "create isolated hardlink fixture");
            bool linkBlocked = false; try { Installer.SafeWrite(linked, new byte[] { 0 }); } catch (IOException) { linkBlocked = true; }
            Check(linkBlocked && File.ReadAllText(outside) == "sentinel", "reject hardlink without modifying outside sentinel");
            Application.EnableVisualStyles();
            using (var form = new SetupForm(@"C:\Program Files\obs-studio")) using (var bitmap = new Bitmap(form.Width, form.Height)) {
                form.ShowInTaskbar = false; form.StartPosition = FormStartPosition.Manual; form.Location = new Point(-10000, -10000);
                form.Show(); Application.DoEvents();
                form.DrawToBitmap(bitmap, new Rectangle(0, 0, bitmap.Width, bitmap.Height));
                bitmap.Save(Path.Combine(root, "installer-preview.png"), System.Drawing.Imaging.ImageFormat.Png);
                int distinct = 0; for (int y = 40; y < bitmap.Height - 20; y += 3) for (int x = 15; x < bitmap.Width - 15; x += 3) if (bitmap.GetPixel(x, y).GetBrightness() < .5) distinct++;
                Check(form.Text == "OBS局部打码插件 · 安装 / 更新" && distinct > 100, "installer UI renders populated controls with dummy public example path");
                form.Close();
            }
            Console.WriteLine("RESULT " + count + " checks, 0 failures"); return 0;
        } catch (Exception e) { Console.WriteLine("FAIL " + e.Message); return 1; }
    }
}
