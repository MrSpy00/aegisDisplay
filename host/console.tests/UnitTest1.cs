using AegisSoft.AegisDisplay.Console.Models;
using AegisSoft.AegisDisplay.Console.Services;

namespace aegisDisplayConsole.Tests;

public class CoreStatusTests
{
    [Fact]
    public void TryParse_ValidStreamingJson_ParsesAllTelemetryFields()
    {
        const string json = """
        {
          "state": "STREAMING",
          "gpuName": "NVIDIA GeForce RTX 5070 Ti",
          "encoderName": "NVENC Direct (HEVC)",
          "clientId": "honor-magic",
          "clientName": "HONOR Magic",
          "clientIp": "192.168.0.50",
          "transportMode": "UDP",
          "driverInstalled": true,
          "virtualDisplayActive": true,
          "width": 1920,
          "height": 1080,
          "refreshHz": 120,
          "targetBitrateKbps": 22000,
          "currentFps": 119.8,
          "encodeMs": 2.41,
          "rttMs": 14.2,
          "lossPermille": 0.5,
          "framesSent": 9876,
          "framesDropped": 3,
          "cursorEnabled": true,
          "useVbr": false,
          "presetLevel": 1,
          "idrIntervalSec": 2,
          "intraRefresh": false,
          "codecOverride": "HEVC",
          "maxFps": 120,
          "monitorPlacement": "LEFT",
          "udpPayloadBytes": 1280,
          "udpPacingEnabled": true
        }
        """;

        var status = CoreStatus.TryParse(json);
        Assert.NotNull(status);
        Assert.True(status.IsStreaming);
        Assert.True(status.IsCoreReachable);
        Assert.Equal("NVIDIA GeForce RTX 5070 Ti", status.GpuName);
        Assert.Equal("NVENC Direct (HEVC)", status.EncoderName);
        Assert.Equal(1920u, status.Width);
        Assert.Equal(1080u, status.Height);
        Assert.Equal(120u, status.RefreshHz);
        Assert.Equal(22000u, status.TargetBitrateKbps);
        Assert.Equal(119.8, status.CurrentFps, 1);
        Assert.Equal(9876ul, status.FramesSent);
        Assert.Equal(3ul, status.FramesDropped);
        Assert.True(status.CursorEnabled);
        Assert.False(status.UseVbr);
        Assert.Equal(1u, status.PresetLevel);
        Assert.Equal(2u, status.IdrIntervalSec);
        Assert.False(status.IntraRefresh);
        Assert.Equal("HEVC", status.CodecOverride);
        Assert.Equal(120u, status.MaxFps);
        Assert.Equal("LEFT", status.MonitorPlacement);
        Assert.Equal(1280u, status.UdpPayloadBytes);
        Assert.True(status.UdpPacingEnabled);
    }

    [Fact]
    public void HostSettingsApplyRequest_SerializesAllKeysForCoreIpc()
    {
        var req = new HostSettingsApplyRequest(
            BitrateKbps: 28000,
            CursorEnabled: true,
            UseVbr: true,
            PresetLevel: 2,
            IdrIntervalSec: 3,
            IntraRefresh: false,
            CodecOverride: "AUTO",
            MaxFps: 90,
            MonitorPlacement: "TOP",
            UdpPayloadBytes: 1280,
            UdpPacingEnabled: true);

        string json = req.ToApplyCommandJson();
        Assert.Contains("\"cmd\":\"apply_settings\"", json);
        Assert.Contains("\"bitrateKbps\":28000", json);
        Assert.Contains("\"cursorEnabled\":true", json);
        Assert.Contains("\"monitorPlacement\":\"TOP\"", json);
        Assert.Contains("\"udpPayloadBytes\":1280", json);
    }

    [Fact]
    public void TryParse_InvalidOrEmptyJson_ReturnsNull()
    {
        Assert.Null(CoreStatus.TryParse(null));
        Assert.Null(CoreStatus.TryParse("   "));
        Assert.Null(CoreStatus.TryParse("{not-valid-json"));
    }

    [Fact]
    public void LocateCoreExecutable_FindsBuiltCoreBinary()
    {
        string? path = HostControlService.LocateCoreExecutable();
        Assert.False(string.IsNullOrWhiteSpace(path));
    }
}
