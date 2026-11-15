#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aegis::ipc {

struct TelemetrySnapshot {
    std::string state{"IDLE"};
    std::string gpuName{"Unknown"};
    std::string encoderName{"None"};
    std::string clientId;
    std::string clientName;
    std::string clientIp;
    std::string transportMode{"NONE"};
    bool driverInstalled{false};
    bool virtualDisplayActive{false};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t refreshHz{0};
    uint32_t targetBitrateKbps{22000};
    double currentFps{0.0};
    double encodeMs{0.0};
    double rttMs{0.0};
    double lossPermille{0.0};
    uint64_t framesSent{0};
    uint64_t framesDropped{0};
    bool cursorEnabled{true};
    bool useVbr{true};
    uint32_t presetLevel{2};
    uint32_t idrIntervalSec{2};
    bool intraRefresh{false};
    std::string codecOverride{"AUTO"};
    uint32_t maxFps{120};
    std::string monitorPlacement{"RIGHT"};
    uint32_t udpPayloadBytes{1280};
    bool udpPacingEnabled{true};
};

struct HostSettingsUpdate {
    uint32_t bitrateKbps{22000};
    bool cursorEnabled{true};
    bool useVbr{true};
    uint32_t presetLevel{2};
    uint32_t idrIntervalSec{2};
    bool intraRefresh{false};
    std::string codecOverride{"AUTO"};
    uint32_t maxFps{120};
    std::string monitorPlacement{"RIGHT"};
    uint32_t udpPayloadBytes{1280};
    bool udpPacingEnabled{true};
};

struct IpcCommandCallbacks {
    std::function<void(uint32_t bitrateKbps)> onSetBitrate;
    std::function<void(const HostSettingsUpdate&)> onApplySettings;
    std::function<void()> onRequestKeyframe;
    std::function<void()> onDisconnectClient;
    std::function<void()> onShutdown;
};

class NamedPipeIpcServer {
public:
    NamedPipeIpcServer() = default;
    ~NamedPipeIpcServer();

    bool start(IpcCommandCallbacks callbacks);
    void stop();
    void updateSnapshot(const TelemetrySnapshot& snap);

private:
    void serverLoop();
    std::string serializeSnapshotJson() const;
    void handleCommandJson(const std::string& json);

    IpcCommandCallbacks callbacks_;
    mutable std::mutex snapMutex_;
    TelemetrySnapshot snapshot_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace aegis::ipc
