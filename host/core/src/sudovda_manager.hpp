#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aegis::vdisplay {

enum class MonitorPlacement : uint8_t {
    Right  = 0,
    Left   = 1,
    Top    = 2,
    Bottom = 3,
};

struct VirtualMonitorHandle {
    GUID guid{};
    LUID adapterLuid{};
    uint32_t targetId{0};
    std::wstring gdiDeviceName;
    HMONITOR hMonitor{nullptr};
    RECT monitorRect{};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t refreshHz{60};
};

class SudoVdaManager {
public:
    SudoVdaManager() = default;
    ~SudoVdaManager();

    SudoVdaManager(const SudoVdaManager&) = delete;
    SudoVdaManager& operator=(const SudoVdaManager&) = delete;

    static bool isDriverInstalled();
    static bool installDriver(const std::filesystem::path& driverDir, std::string& errorOut);
    static bool uninstallDriver(std::string& errorOut);
    static bool detectHighPerformanceRenderAdapter(std::string& gpuNameOut, LUID& luidOut);
    static GUID deriveDeviceGuid(const std::string& deviceId);

    bool open(std::string& errorOut);
    void close();
    bool isOpen() const;

    bool bindHighPerformanceRenderAdapter(std::string& gpuNameOut, LUID& luidOut);

    std::optional<VirtualMonitorHandle> addMonitor(
        const std::string& deviceId,
        const std::string& deviceName,
        uint32_t width,
        uint32_t height,
        uint32_t refreshHz,
        std::string& errorOut);

    bool removeMonitor(const GUID& guid);
    void removeAllManagedMonitors();

    bool startWatchdog(std::function<void()> onWatchdogFailure = nullptr);
    void stopWatchdog();

    static bool refreshMonitorRect(VirtualMonitorHandle& mon);
    static bool applyMonitorPlacement(VirtualMonitorHandle& mon, MonitorPlacement placement);

private:
    bool findDisplayByAdapterAndTarget(
        const LUID& adapterLuid,
        uint32_t targetId,
        std::wstring& outGdiName,
        HMONITOR& outHMonitor,
        RECT& outRect) const;

    bool applyMonitorMode(const std::wstring& gdiName, uint32_t width, uint32_t height, uint32_t refreshHz) const;

    mutable std::mutex mutex_;
    HANDLE handle_{INVALID_HANDLE_VALUE};
    GUID activeMonitorGuid_{};
    bool hasActiveMonitor_{false};

    std::atomic<bool> watchdogRunning_{false};
    std::thread watchdogThread_;
};

} // namespace aegis::vdisplay
