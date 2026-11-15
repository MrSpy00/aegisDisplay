using System.ComponentModel;
using System.Diagnostics;
using System.IO;

namespace AegisSoft.AegisDisplay.Console.Services;

public sealed record CommandResult(bool Success, string Message);

public sealed class HostControlService
{
    public static string? LocateCoreExecutable()
    {
        string baseDir = AppContext.BaseDirectory;
        var dir = new DirectoryInfo(baseDir);

        string[] relativeCandidates =
        [
            "aegisDisplayCore.exe",
            Path.Combine("core", "aegisDisplayCore.exe"),
            Path.Combine("..", "core", "build", "aegisDisplayCore.exe"),
            Path.Combine("..", "..", "core", "build", "aegisDisplayCore.exe"),
            Path.Combine("..", "..", "..", "..", "core", "build", "aegisDisplayCore.exe"),
            Path.Combine("..", "..", "..", "..", "..", "core", "build", "aegisDisplayCore.exe"),
        ];

        foreach (string rel in relativeCandidates)
        {
            string full = Path.GetFullPath(Path.Combine(baseDir, rel));
            if (File.Exists(full))
            {
                return full;
            }
        }

        // Walk upward up to 6 levels looking for host/core/build/aegisDisplayCore.exe
        DirectoryInfo? cur = dir;
        for (int i = 0; i < 6 && cur is not null; i++)
        {
            string probe1 = Path.Combine(cur.FullName, "core", "build", "aegisDisplayCore.exe");
            if (File.Exists(probe1)) return probe1;
            string probe2 = Path.Combine(cur.FullName, "host", "core", "build", "aegisDisplayCore.exe");
            if (File.Exists(probe2)) return probe2;
            cur = cur.Parent;
        }
        return null;
    }

    public static bool IsCoreProcessRunning() =>
        Process.GetProcessesByName("aegisDisplayCore").Length > 0;

    public static CommandResult StartCoreDaemon()
    {
        if (IsCoreProcessRunning())
        {
            return new CommandResult(true, "Core servisi zaten çalışıyor.");
        }

        string? exe = LocateCoreExecutable();
        if (exe is null)
        {
            return new CommandResult(false, "aegisDisplayCore.exe bulunamadı.");
        }

        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = "--daemon",
                WorkingDirectory = Path.GetDirectoryName(exe)!,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            Process.Start(psi);
            return new CommandResult(true, $"Core başlatıldı: {Path.GetFileName(exe)}");
        }
        catch (Exception ex)
        {
            return new CommandResult(false, $"Core başlatılamadı: {ex.Message}");
        }
    }

    public static async Task<CommandResult> RunElevatedCoreCommandAsync(string argument)
    {
        string? exe = LocateCoreExecutable();
        if (exe is null)
        {
            return new CommandResult(false, "aegisDisplayCore.exe bulunamadı.");
        }

        return await Task.Run(() =>
        {
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = exe,
                    Arguments = argument,
                    WorkingDirectory = Path.GetDirectoryName(exe)!,
                    UseShellExecute = true,
                    Verb = "runas"
                };
                using var proc = Process.Start(psi);
                if (proc is null)
                {
                    return new CommandResult(false, "İşlem başlatılamadı.");
                }
                proc.WaitForExit(30000);
                return proc.ExitCode == 0
                    ? new CommandResult(true, $"Sürücü işlemi başarıyla tamamlandı ({argument}).")
                    : new CommandResult(false, $"Sürücü işlemi başarısız (kod {proc.ExitCode}).");
            }
            catch (Win32Exception ex) when (ex.NativeErrorCode == 1223)
            {
                return new CommandResult(false, "UAC yetki onayı kullanıcı tarafından iptal edildi.");
            }
            catch (Exception ex)
            {
                return new CommandResult(false, $"Sürücü hatası: {ex.Message}");
            }
        }).ConfigureAwait(false);
    }

    public static async Task<CommandResult> SetupUsbAdbTunnelAsync()
    {
        return await Task.Run(() =>
        {
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = "adb",
                    Arguments = "reverse tcp:28451 tcp:28451",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };
                using var proc = Process.Start(psi);
                if (proc is null)
                {
                    return new CommandResult(false, "adb çalıştırılamadı.");
                }
                string stderr = proc.StandardError.ReadToEnd().Trim();
                proc.WaitForExit(5000);
                if (proc.ExitCode == 0)
                {
                    return new CommandResult(true, "USB ADB tüneli aktif: tcp:28451 -> tcp:28451");
                }
                return new CommandResult(false, string.IsNullOrEmpty(stderr) ? $"adb çıkış kodu {proc.ExitCode}" : stderr);
            }
            catch (Exception ex)
            {
                return new CommandResult(false, $"ADB bulunamadı veya hata verdi: {ex.Message}");
            }
        }).ConfigureAwait(false);
    }

    public static void OpenWindowsDisplaySettings()
    {
        try
        {
            Process.Start(new ProcessStartInfo("ms-settings:display") { UseShellExecute = true });
        }
        catch
        {
            // ignore
        }
    }
}
