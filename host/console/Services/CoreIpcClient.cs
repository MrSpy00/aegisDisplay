using System.IO;
using System.IO.Pipes;
using System.Text;
using AegisSoft.AegisDisplay.Console.Models;

namespace AegisSoft.AegisDisplay.Console.Services;

public sealed class CoreIpcClient
{
    public const string PipeName = "aegisDisplayIpcV1";

    public Task<CoreStatus?> GetStatusAsync(CancellationToken ct = default) =>
        SendCommandAsync("{\"cmd\":\"get_status\"}", ct);

    public Task<CoreStatus?> SetBitrateAsync(uint kbps, CancellationToken ct = default)
    {
        uint clamped = Math.Clamp(kbps, 2000u, 100000u);
        return SendCommandAsync($"{{\"cmd\":\"set_bitrate\",\"kbps\":{clamped}}}", ct);
    }

    public Task<CoreStatus?> ApplySettingsAsync(HostSettingsApplyRequest request, CancellationToken ct = default) =>
        SendCommandAsync(request.ToApplyCommandJson(), ct);

    public Task<CoreStatus?> RequestKeyframeAsync(CancellationToken ct = default) =>
        SendCommandAsync("{\"cmd\":\"request_keyframe\"}", ct);

    public Task<CoreStatus?> DisconnectClientAsync(CancellationToken ct = default) =>
        SendCommandAsync("{\"cmd\":\"disconnect\"}", ct);

    public Task<CoreStatus?> ShutdownCoreAsync(CancellationToken ct = default) =>
        SendCommandAsync("{\"cmd\":\"shutdown\"}", ct);

    private static async Task<CoreStatus?> SendCommandAsync(string payloadJson, CancellationToken ct)
    {
        return await Task.Run(async () =>
        {
            try
            {
                using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.None);
                await pipe.ConnectAsync(450, ct);

                byte[] payload = Encoding.UTF8.GetBytes(payloadJson + "\n");
                await pipe.WriteAsync(payload, ct);
                await pipe.FlushAsync(ct);

                using var reader = new StreamReader(pipe, Encoding.UTF8, detectEncodingFromByteOrderMarks: false, bufferSize: 4096, leaveOpen: true);
                string? line = await reader.ReadLineAsync(ct);
                return CoreStatus.TryParse(line);
            }
            catch
            {
                return null;
            }
        }, ct);
    }
}
