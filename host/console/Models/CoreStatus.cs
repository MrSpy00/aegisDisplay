using System.Text.Json;
using System.Text.Json.Serialization;

namespace AegisSoft.AegisDisplay.Console.Models;

public sealed record CoreStatus
{
    [JsonPropertyName("state")]
    public string State { get; init; } = "OFFLINE";

    [JsonPropertyName("gpuName")]
    public string GpuName { get; init; } = "-";

    [JsonPropertyName("encoderName")]
    public string EncoderName { get; init; } = "-";

    [JsonPropertyName("clientId")]
    public string ClientId { get; init; } = "";

    [JsonPropertyName("clientName")]
    public string ClientName { get; init; } = "";

    [JsonPropertyName("clientIp")]
    public string ClientIp { get; init; } = "";

    [JsonPropertyName("transportMode")]
    public string TransportMode { get; init; } = "";

    [JsonPropertyName("driverInstalled")]
    public bool DriverInstalled { get; init; }

    [JsonPropertyName("virtualDisplayActive")]
    public bool VirtualDisplayActive { get; init; }

    [JsonPropertyName("width")]
    public uint Width { get; init; }

    [JsonPropertyName("height")]
    public uint Height { get; init; }

    [JsonPropertyName("refreshHz")]
    public uint RefreshHz { get; init; }

    [JsonPropertyName("targetBitrateKbps")]
    public uint TargetBitrateKbps { get; init; }

    [JsonPropertyName("currentFps")]
    public double CurrentFps { get; init; }

    [JsonPropertyName("encodeMs")]
    public double EncodeMs { get; init; }

    [JsonPropertyName("rttMs")]
    public double RttMs { get; init; }

    [JsonPropertyName("lossPermille")]
    public double LossPermille { get; init; }

    [JsonPropertyName("framesSent")]
    public ulong FramesSent { get; init; }

    [JsonPropertyName("framesDropped")]
    public ulong FramesDropped { get; init; }

    [JsonPropertyName("cursorEnabled")]
    public bool CursorEnabled { get; init; } = true;

    [JsonPropertyName("useVbr")]
    public bool UseVbr { get; init; } = true;

    [JsonPropertyName("presetLevel")]
    public uint PresetLevel { get; init; } = 2;

    [JsonPropertyName("idrIntervalSec")]
    public uint IdrIntervalSec { get; init; } = 2;

    [JsonPropertyName("intraRefresh")]
    public bool IntraRefresh { get; init; }

    [JsonPropertyName("codecOverride")]
    public string CodecOverride { get; init; } = "AUTO";

    [JsonPropertyName("maxFps")]
    public uint MaxFps { get; init; } = 120;

    [JsonPropertyName("monitorPlacement")]
    public string MonitorPlacement { get; init; } = "RIGHT";

    [JsonPropertyName("udpPayloadBytes")]
    public uint UdpPayloadBytes { get; init; } = 1280;

    [JsonPropertyName("udpPacingEnabled")]
    public bool UdpPacingEnabled { get; init; } = true;

    public bool IsStreaming => string.Equals(State, "STREAMING", StringComparison.OrdinalIgnoreCase);

    public bool IsCoreReachable => !string.Equals(State, "OFFLINE", StringComparison.OrdinalIgnoreCase);

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public static CoreStatus? TryParse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return null;
        }

        try
        {
            return JsonSerializer.Deserialize<CoreStatus>(json.Trim(), JsonOptions);
        }
        catch (JsonException)
        {
            return null;
        }
    }
}

public sealed record HostSettingsApplyRequest(
    [property: JsonPropertyName("bitrateKbps")] uint BitrateKbps,
    [property: JsonPropertyName("cursorEnabled")] bool CursorEnabled,
    [property: JsonPropertyName("useVbr")] bool UseVbr,
    [property: JsonPropertyName("presetLevel")] uint PresetLevel,
    [property: JsonPropertyName("idrIntervalSec")] uint IdrIntervalSec,
    [property: JsonPropertyName("intraRefresh")] bool IntraRefresh,
    [property: JsonPropertyName("codecOverride")] string CodecOverride,
    [property: JsonPropertyName("maxFps")] uint MaxFps,
    [property: JsonPropertyName("monitorPlacement")] string MonitorPlacement,
    [property: JsonPropertyName("udpPayloadBytes")] uint UdpPayloadBytes,
    [property: JsonPropertyName("udpPacingEnabled")] bool UdpPacingEnabled)
{
    public string ToApplyCommandJson() => JsonSerializer.Serialize(new
    {
        cmd = "apply_settings",
        bitrateKbps = BitrateKbps,
        cursorEnabled = CursorEnabled,
        useVbr = UseVbr,
        presetLevel = PresetLevel,
        idrIntervalSec = IdrIntervalSec,
        intraRefresh = IntraRefresh,
        codecOverride = CodecOverride,
        maxFps = MaxFps,
        monitorPlacement = MonitorPlacement,
        udpPayloadBytes = UdpPayloadBytes,
        udpPacingEnabled = UdpPacingEnabled
    });
}

public static class QualityPresets
{
    public const uint LowLatencyKbps = 12000;
    public const uint BalancedKbps = 22000;
    public const uint StudioKbps = 45000;
}
