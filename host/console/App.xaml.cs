using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows;
using AegisSoft.AegisDisplay.Console.Services;
using Forms = System.Windows.Forms;

namespace AegisSoft.AegisDisplay.Console;

public partial class App : System.Windows.Application
{
    private Forms.NotifyIcon? _trayIcon;
    private MainWindow? _mainWindow;
    private readonly CoreIpcClient _ipc = new();
    private bool _exiting;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        HostControlService.StartCoreDaemon();

        _mainWindow = new MainWindow(_ipc);
        _mainWindow.Closing += (s, args) =>
        {
            if (!_exiting)
            {
                args.Cancel = true;
                _mainWindow.Hide();
            }
        };

        SetupTrayIcon();
        _mainWindow.Show();
    }

    private void SetupTrayIcon()
    {
        _trayIcon = new Forms.NotifyIcon
        {
            Text = "aegisDisplay Host — aegisSoft",
            Icon = CreateVectorTrayIcon(),
            Visible = true
        };

        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Konsolu Aç", null, (_, _) => ShowMainWindow());
        menu.Items.Add("USB ADB Tüneli Kur (28451)", null, async (_, _) =>
        {
            var res = await HostControlService.SetupUsbAdbTunnelAsync();
            _trayIcon?.ShowBalloonTip(2500, "aegisDisplay", res.Message,
                res.Success ? Forms.ToolTipIcon.Info : Forms.ToolTipIcon.Warning);
        });
        menu.Items.Add("Ekran Ayarlarını Aç", null, (_, _) => HostControlService.OpenWindowsDisplaySettings());
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Çıkış (Host ve Core Kapat)", null, async (_, _) => await ExitAllAsync());

        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.DoubleClick += (_, _) => ShowMainWindow();
    }

    private void ShowMainWindow()
    {
        if (_mainWindow is null) return;
        if (!_mainWindow.IsVisible)
        {
            _mainWindow.Show();
        }
        if (_mainWindow.WindowState == WindowState.Minimized)
        {
            _mainWindow.WindowState = WindowState.Normal;
        }
        _mainWindow.Activate();
    }

    public async Task ExitAllAsync()
    {
        _exiting = true;
        try
        {
            await _ipc.ShutdownCoreAsync();
        }
        catch
        {
            // ignore
        }
        if (_trayIcon is not null)
        {
            _trayIcon.Visible = false;
            _trayIcon.Dispose();
            _trayIcon = null;
        }
        _mainWindow?.Close();
        Shutdown();
    }

    private static Icon CreateVectorTrayIcon()
    {
        try
        {
            var streamInfo = GetResourceStream(new Uri("pack://application:,,,/Assets/aegisdisplay.ico"));
            if (streamInfo?.Stream is not null)
            {
                using var s = streamInfo.Stream;
                return new Icon(s, new System.Drawing.Size(32, 32));
            }
        }
        catch
        {
            // Fall through to GDI+ fallback
        }

        using var bmp = new Bitmap(32, 32);
        using var g = Graphics.FromImage(bmp);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Color.Transparent);

        using var bgBrush = new SolidBrush(Color.FromArgb(255, 11, 25, 44));
        using var borderPen = new Pen(Color.FromArgb(255, 34, 211, 238), 2.5f);
        g.FillRectangle(bgBrush, 3, 5, 26, 18);
        g.DrawRectangle(borderPen, 3, 5, 26, 18);

        using var standBrush = new SolidBrush(Color.FromArgb(255, 0, 184, 212));
        g.FillRectangle(standBrush, 13, 23, 6, 3);
        g.FillRectangle(standBrush, 9, 26, 14, 2);

        return Icon.FromHandle(bmp.GetHicon());
    }
}
