using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using AegisSoft.AegisDisplay.Console.Models;
using AegisSoft.AegisDisplay.Console.Services;

namespace AegisSoft.AegisDisplay.Console;

public partial class MainWindow : Window
{
    private readonly CoreIpcClient _ipc;
    private readonly DispatcherTimer _pollTimer;
    private bool _updatingSliderFromCore;
    private bool _updatingControlsFromCore;
    private bool _settingsDirty;
    private bool _hasInitialSettingsSync;

    public MainWindow() : this(new CoreIpcClient())
    {
    }

    public MainWindow(CoreIpcClient ipc)
    {
        InitializeComponent();
        _ipc = ipc;
        _pollTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(500)
        };
        _pollTimer.Tick += async (_, _) => await RefreshStatusAsync();
        _pollTimer.Start();
        Loaded += async (_, _) => await RefreshStatusAsync();
    }

    private async Task RefreshStatusAsync()
    {
        var status = await _ipc.GetStatusAsync();
        if (status is null)
        {
            ApplyOfflineUi();
            return;
        }
        ApplyStatusUi(status);
    }

    private void ApplyOfflineUi()
    {
        StateBadgeText.Text = "CORE KAPALI";
        StateDot.Fill = (System.Windows.Media.Brush)FindResource("StatusOfflineBrush");
        GpuText.Text = "-";
        EncoderText.Text = "aegisDisplayCore.exe çalışmıyor";
        DriverStatusText.Text = "-";
        VirtualDisplayText.Text = "Sanal monitör pasif";
        ClientNameText.Text = "Bekleniyor";
        ClientEndpointText.Text = "UDP 28450 / TCP 28451";
        FpsText.Text = "0.0";
        EncodeMsText.Text = "0.0 ms";
        RttMsText.Text = "0.0 ms";
        LossText.Text = "0.0 ‰";
        FramesDroppedText.Text = "0";
        ActiveBitrateText.Text = "-";
        ResolutionText.Text = "-";
        KeyframeBtn.IsEnabled = false;
        DisconnectBtn.IsEnabled = false;
    }

    private void ApplyStatusUi(CoreStatus s)
    {
        if (s.IsStreaming)
        {
            StateBadgeText.Text = "YAYIN AKTİF";
            StateDot.Fill = (System.Windows.Media.Brush)FindResource("StatusGoodBrush");
        }
        else
        {
            StateBadgeText.Text = "HAZIR (IDLE)";
            StateDot.Fill = (System.Windows.Media.Brush)FindResource("StatusWarnBrush");
        }

        GpuText.Text = string.IsNullOrWhiteSpace(s.GpuName) ? "-" : s.GpuName;
        EncoderText.Text = string.IsNullOrWhiteSpace(s.EncoderName) ? "-" : s.EncoderName;

        DriverStatusText.Text = s.DriverInstalled ? "SudoVDA Kurulu" : "Sürücü Kurulu Değil";
        VirtualDisplayText.Text = s.VirtualDisplayActive
            ? $"Aktif ({s.Width}x{s.Height} @ {s.RefreshHz} Hz · {s.MonitorPlacement})"
            : $"Sanal monitör beklemede ({s.MonitorPlacement})";

        ClientNameText.Text = string.IsNullOrWhiteSpace(s.ClientName) ? "Bağlı cihaz yok" : s.ClientName;
        ClientEndpointText.Text = string.IsNullOrWhiteSpace(s.ClientIp)
            ? "UDP 28450 / TCP 28451"
            : $"{s.ClientIp} ({s.TransportMode})";

        FpsText.Text = $"{s.CurrentFps:F1}";
        EncodeMsText.Text = $"{s.EncodeMs:F2} ms";
        RttMsText.Text = $"{s.RttMs:F1} ms";
        LossText.Text = $"{s.LossPermille:F1} ‰";
        FramesDroppedText.Text = $"{s.FramesDropped}";
        ActiveBitrateText.Text = $"{s.TargetBitrateKbps / 1000.0:F0} Mbps";
        ResolutionText.Text = s.Width > 0 ? $"{s.Width}x{s.Height}@{s.RefreshHz}" : "-";

        KeyframeBtn.IsEnabled = s.IsStreaming;
        DisconnectBtn.IsEnabled = s.IsStreaming;

        if (!_hasInitialSettingsSync || !_settingsDirty)
        {
            SyncControlsFromStatus(s);
            _hasInitialSettingsSync = true;
        }
    }

    private void SyncControlsFromStatus(CoreStatus s)
    {
        _updatingControlsFromCore = true;
        try
        {
            if (s.TargetBitrateKbps >= 4000 && s.TargetBitrateKbps <= 60000)
            {
                _updatingSliderFromCore = true;
                BitrateSlider.Value = s.TargetBitrateKbps;
                BitrateValueText.Text = $"{s.TargetBitrateKbps / 1000.0:F0} Mbps";
                _updatingSliderFromCore = false;
            }

            CursorCheck.IsChecked = s.CursorEnabled;
            UdpPacingCheck.IsChecked = s.UdpPacingEnabled;
            IntraRefreshCheck.IsChecked = s.IntraRefresh;

            SelectComboByTag(RateControlCombo, s.UseVbr ? "VBR" : "CBR");
            SelectComboByTag(PresetCombo, s.PresetLevel.ToString());
            SelectComboByTag(CodecCombo, string.IsNullOrWhiteSpace(s.CodecOverride) ? "AUTO" : s.CodecOverride);
            SelectComboByTag(MaxFpsCombo, s.MaxFps.ToString());
            SelectComboByTag(IdrIntervalCombo, s.IdrIntervalSec.ToString());
            SelectComboByTag(PlacementCombo, string.IsNullOrWhiteSpace(s.MonitorPlacement) ? "RIGHT" : s.MonitorPlacement);
            SelectComboByTag(UdpPayloadCombo, s.UdpPayloadBytes.ToString());
        }
        finally
        {
            _updatingControlsFromCore = false;
        }
    }

    private static void SelectComboByTag(System.Windows.Controls.ComboBox combo, string tag)
    {
        foreach (var item in combo.Items)
        {
            if (item is ComboBoxItem cbi &&
                string.Equals(cbi.Tag?.ToString(), tag, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedItem = cbi;
                return;
            }
        }
    }

    private static string GetSelectedTag(System.Windows.Controls.ComboBox combo, string fallback)
    {
        if (combo.SelectedItem is ComboBoxItem cbi && cbi.Tag?.ToString() is { Length: > 0 } tag)
        {
            return tag;
        }
        return fallback;
    }

    private void BitrateSlider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (BitrateValueText is null || _updatingSliderFromCore) return;
        BitrateValueText.Text = $"{e.NewValue / 1000.0:F0} Mbps";
        if (!_updatingControlsFromCore)
        {
            _settingsDirty = true;
        }
    }

    private void SettingControl_Changed(object sender, RoutedEventArgs e)
    {
        if (_updatingControlsFromCore || !IsLoaded) return;
        _settingsDirty = true;
    }

    private async void PresetLowLatency_Click(object sender, RoutedEventArgs e)
    {
        BitrateSlider.Value = QualityPresets.LowLatencyKbps;
        await ApplyBitrateInternalAsync(QualityPresets.LowLatencyKbps);
    }

    private async void PresetBalanced_Click(object sender, RoutedEventArgs e)
    {
        BitrateSlider.Value = QualityPresets.BalancedKbps;
        await ApplyBitrateInternalAsync(QualityPresets.BalancedKbps);
    }

    private async void PresetStudio_Click(object sender, RoutedEventArgs e)
    {
        BitrateSlider.Value = QualityPresets.StudioKbps;
        await ApplyBitrateInternalAsync(QualityPresets.StudioKbps);
    }

    private async void ApplyBitrate_Click(object sender, RoutedEventArgs e)
    {
        await ApplyBitrateInternalAsync((uint)BitrateSlider.Value);
    }

    private async Task ApplyBitrateInternalAsync(uint kbps)
    {
        var res = await _ipc.SetBitrateAsync(kbps);
        ActionLogText.Text = res is not null
            ? $"Hedef bit hızı {kbps / 1000.0:F0} Mbps olarak uygulandı."
            : "Core motoruna bağlanılamadı (bit hızı uygulanamadı).";
        if (res is not null) ApplyStatusUi(res);
    }

    private async void SyncSettings_Click(object sender, RoutedEventArgs e)
    {
        _settingsDirty = false;
        var res = await _ipc.GetStatusAsync();
        if (res is not null)
        {
            SyncControlsFromStatus(res);
            ApplyStatusUi(res);
            ActionLogText.Text = "Ayarlar Core motorundan yeniden senkronize edildi.";
        }
        else
        {
            ActionLogText.Text = "Core motoru çevrimdışı; ayarlar okunamadı.";
        }
    }

    private async void ApplySettings_Click(object sender, RoutedEventArgs e)
    {
        uint bitrateKbps = (uint)BitrateSlider.Value;
        bool cursorEnabled = CursorCheck.IsChecked == true;
        bool useVbr = string.Equals(GetSelectedTag(RateControlCombo, "VBR"), "VBR", StringComparison.OrdinalIgnoreCase);
        uint presetLevel = uint.TryParse(GetSelectedTag(PresetCombo, "2"), out uint p) ? p : 2;
        uint idrSec = uint.TryParse(GetSelectedTag(IdrIntervalCombo, "2"), out uint idr) ? idr : 2;
        bool intraRefresh = IntraRefreshCheck.IsChecked == true;
        string codecOverride = GetSelectedTag(CodecCombo, "AUTO");
        uint maxFps = uint.TryParse(GetSelectedTag(MaxFpsCombo, "120"), out uint fps) ? fps : 120;
        string placement = GetSelectedTag(PlacementCombo, "RIGHT");
        uint udpBytes = uint.TryParse(GetSelectedTag(UdpPayloadCombo, "1280"), out uint mtu) ? mtu : 1280;
        bool udpPacing = UdpPacingCheck.IsChecked == true;

        var req = new HostSettingsApplyRequest(
            BitrateKbps: bitrateKbps,
            CursorEnabled: cursorEnabled,
            UseVbr: useVbr,
            PresetLevel: presetLevel,
            IdrIntervalSec: idrSec,
            IntraRefresh: intraRefresh,
            CodecOverride: codecOverride,
            MaxFps: maxFps,
            MonitorPlacement: placement,
            UdpPayloadBytes: udpBytes,
            UdpPacingEnabled: udpPacing);

        var res = await _ipc.ApplySettingsAsync(req);
        if (res is not null)
        {
            _settingsDirty = false;
            ApplyStatusUi(res);
            ActionLogText.Text = "Tüm gelişmiş sunucu/encoder ayarları uygulandı ve kaydedildi.";
        }
        else
        {
            ActionLogText.Text = "Core motoruna bağlanılamadı (ayarlar gönderilemedi).";
        }
    }

    private async void SendKeyframe_Click(object sender, RoutedEventArgs e)
    {
        var res = await _ipc.RequestKeyframeAsync();
        ActionLogText.Text = res is not null ? "IDR Keyframe talep edildi." : "Core yanıt vermedi.";
    }

    private async void DisconnectClient_Click(object sender, RoutedEventArgs e)
    {
        var res = await _ipc.DisconnectClientAsync();
        ActionLogText.Text = res is not null ? "İstemci oturumu kapatıldı." : "Core yanıt vermedi.";
    }

    private async void SetupAdb_Click(object sender, RoutedEventArgs e)
    {
        ActionLogText.Text = "USB ADB tüneli kuruluyor...";
        var res = await HostControlService.SetupUsbAdbTunnelAsync();
        ActionLogText.Text = res.Message;
    }

    private async void InstallDriver_Click(object sender, RoutedEventArgs e)
    {
        ActionLogText.Text = "SudoVDA sürücü kurulumu başlatılıyor (UAC onayı gerekebilir)...";
        var res = await HostControlService.RunElevatedCoreCommandAsync("--install-driver");
        ActionLogText.Text = res.Message;
        await RefreshStatusAsync();
    }

    private async void UninstallDriver_Click(object sender, RoutedEventArgs e)
    {
        ActionLogText.Text = "SudoVDA sürücüsü kaldırılıyor (UAC onayı gerekebilir)...";
        var res = await HostControlService.RunElevatedCoreCommandAsync("--uninstall-driver");
        ActionLogText.Text = res.Message;
        await RefreshStatusAsync();
    }

    private void OpenDisplaySettings_Click(object sender, RoutedEventArgs e) =>
        HostControlService.OpenWindowsDisplaySettings();

    private async void StartCore_Click(object sender, RoutedEventArgs e)
    {
        var res = HostControlService.StartCoreDaemon();
        ActionLogText.Text = res.Message;
        await Task.Delay(500);
        await RefreshStatusAsync();
    }

    private void HideToTray_Click(object sender, RoutedEventArgs e) => Hide();
}
