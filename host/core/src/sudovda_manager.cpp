#include "sudovda_manager.hpp"

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <newdev.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "sudovda-ioctl.h"

using Microsoft::WRL::ComPtr;

namespace aegis::vdisplay {

namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

void copyAscii13(char (&dst)[14], const std::string& src) {
    std::memset(dst, 0, sizeof(dst));
    size_t idx = 0;
    for (unsigned char c : src) {
        if (idx >= 13) break;
        if (c >= 0x20 && c <= 0x7E) {
            dst[idx++] = static_cast<char>(c);
        }
    }
    if (idx == 0) {
        std::memcpy(dst, "aegisDisp", 9);
    }
}

std::optional<std::wstring> findSudoVdaDevicePath() {
    HDEVINFO set = SetupDiGetClassDevsW(
        &SUDOVDA::SUVDA_INTERFACE_GUID,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<std::wstring> result;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = sizeof(iface);
    if (SetupDiEnumDeviceInterfaces(set, nullptr, &SUDOVDA::SUVDA_INTERFACE_GUID, 0, &iface)) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &required, nullptr);
        if (required >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            std::vector<uint8_t> buf(required);
            auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, required, nullptr, nullptr)) {
                result = std::wstring(detail->DevicePath);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

bool importCertToStore(const std::filesystem::path& cerPath, const wchar_t* storeName, std::string& errOut) {
    HANDLE hFile = CreateFileW(cerPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        errOut = "Cannot open certificate file: " + cerPath.string();
        return false;
    }
    DWORD size = GetFileSize(hFile, nullptr);
    std::vector<uint8_t> der(size);
    DWORD read = 0;
    BOOL okRead = ReadFile(hFile, der.data(), size, &read, nullptr);
    CloseHandle(hFile);
    if (!okRead || read == 0) {
        errOut = "Failed reading certificate bytes";
        return false;
    }

    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE,
        storeName);
    if (!store) {
        errOut = "CertOpenStore failed (requires Administrator privileges), win32=" + std::to_string(GetLastError());
        return false;
    }

    BOOL added = CertAddEncodedCertificateToStore(
        store,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        der.data(),
        read,
        CERT_STORE_ADD_REPLACE_EXISTING,
        nullptr);
    DWORD err = GetLastError();
    CertCloseStore(store, 0);
    if (!added) {
        errOut = "CertAddEncodedCertificateToStore failed, win32=" + std::to_string(err);
        return false;
    }
    return true;
}

} // namespace

SudoVdaManager::~SudoVdaManager() {
    close();
}

GUID SudoVdaManager::deriveDeviceGuid(const std::string& deviceId) {
    const std::string input = "aegisSoft.aegisDisplay.monitor.v1:" + deviceId;
    uint8_t digest[32]{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        BCRYPT_HASH_HANDLE h = nullptr;
        if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0))) {
            BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), 0);
            BCryptFinishHash(h, digest, sizeof(digest), 0);
            BCryptDestroyHash(h);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    } else {
        for (size_t i = 0; i < input.size(); ++i) {
            digest[i % 16] ^= static_cast<uint8_t>(input[i] * 31u + i);
        }
    }
    // Stamp RFC 4122 v4/v5-style bits cleanly
    digest[6] = (digest[6] & 0x0Fu) | 0x50u;
    digest[8] = (digest[8] & 0x3Fu) | 0x80u;
    GUID g{};
    std::memcpy(&g, digest, sizeof(GUID));
    return g;
}

bool SudoVdaManager::isDriverInstalled() {
    return findSudoVdaDevicePath().has_value();
}

bool SudoVdaManager::installDriver(const std::filesystem::path& driverDir, std::string& errorOut) {
    const auto infPath = std::filesystem::absolute(driverDir / L"SudoVDA.inf");
    const auto cerPath = std::filesystem::absolute(driverDir / L"sudovda.cer");
    if (!std::filesystem::exists(infPath) || !std::filesystem::exists(cerPath)) {
        errorOut = "Missing SudoVDA.inf or sudovda.cer in " + driverDir.string();
        return false;
    }

    if (!importCertToStore(cerPath, L"Root", errorOut)) return false;
    if (!importCertToStore(cerPath, L"TrustedPublisher", errorOut)) return false;

    if (!isDriverInstalled()) {
        GUID classGuid = SUDOVDA::SUVDA_CLASS_GUID;
        HDEVINFO devInfo = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
        if (devInfo == INVALID_HANDLE_VALUE) {
            errorOut = "SetupDiCreateDeviceInfoList failed, win32=" + std::to_string(GetLastError());
            return false;
        }
        SP_DEVINFO_DATA devData{};
        devData.cbSize = sizeof(devData);
        if (!SetupDiCreateDeviceInfoW(
                devInfo,
                L"Display",
                &classGuid,
                L"aegisDisplay SudoVDA Adapter",
                nullptr,
                DICD_GENERATE_ID,
                &devData)) {
            errorOut = "SetupDiCreateDeviceInfoW failed (run elevated as Administrator), win32=" + std::to_string(GetLastError());
            SetupDiDestroyDeviceInfoList(devInfo);
            return false;
        }

        // HardwareID is MULTI_SZ with trailing double null
        const wchar_t hwId[] = L"Root\\SudoMaker\\SudoVDA\0";
        if (!SetupDiSetDeviceRegistryPropertyW(
                devInfo,
                &devData,
                SPDRP_HARDWAREID,
                reinterpret_cast<const BYTE*>(hwId),
                sizeof(hwId))) {
            errorOut = "SetupDiSetDeviceRegistryPropertyW failed, win32=" + std::to_string(GetLastError());
            SetupDiDestroyDeviceInfoList(devInfo);
            return false;
        }

        if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devInfo, &devData)) {
            errorOut = "SetupDiCallClassInstaller(DIF_REGISTERDEVICE) failed, win32=" + std::to_string(GetLastError());
            SetupDiDestroyDeviceInfoList(devInfo);
            return false;
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    BOOL rebootRequired = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(
            nullptr,
            L"Root\\SudoMaker\\SudoVDA",
            infPath.c_str(),
            INSTALLFLAG_FORCE,
            &rebootRequired)) {
        errorOut = "UpdateDriverForPlugAndPlayDevicesW failed, win32=" + std::to_string(GetLastError());
        return false;
    }
    return true;
}

bool SudoVdaManager::uninstallDriver(std::string& errorOut) {
    GUID classGuid = SUDOVDA::SUVDA_CLASS_GUID;
    HDEVINFO devInfo = SetupDiGetClassDevsW(&classGuid, nullptr, nullptr, 0);
    if (devInfo == INVALID_HANDLE_VALUE) {
        errorOut = "SetupDiGetClassDevsW failed";
        return false;
    }
    bool removedAny = false;
    SP_DEVINFO_DATA devData{};
    devData.cbSize = sizeof(devData);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
        wchar_t buf[512]{};
        if (SetupDiGetDeviceRegistryPropertyW(
                devInfo, &devData, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<BYTE*>(buf), sizeof(buf), nullptr)) {
            if (_wcsicmp(buf, L"Root\\SudoMaker\\SudoVDA") == 0) {
                if (SetupDiCallClassInstaller(DIF_REMOVE, devInfo, &devData)) {
                    removedAny = true;
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    if (!removedAny) {
        errorOut = "No Root\\SudoMaker\\SudoVDA device node found";
    }
    return removedAny;
}

bool SudoVdaManager::open(std::string& errorOut) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ != INVALID_HANDLE_VALUE) {
        return true;
    }

    auto pathOpt = findSudoVdaDevicePath();
    if (!pathOpt.has_value()) {
        errorOut = "SudoVDA driver interface not found. Run --install-driver as Administrator first.";
        return false;
    }

    HANDLE h = CreateFileW(
        pathOpt->c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        errorOut = "CreateFileW(SudoVDA) failed, win32=" + std::to_string(GetLastError());
        return false;
    }

    SUDOVDA::VIRTUAL_DISPLAY_GET_PROTOCOL_VERSION_OUT verOut{};
    DWORD returned = 0;
    if (!DeviceIoControl(h, IOCTL_GET_PROTOCOL_VERSION, nullptr, 0, &verOut, sizeof(verOut), &returned, nullptr)) {
        errorOut = "IOCTL_GET_PROTOCOL_VERSION failed, win32=" + std::to_string(GetLastError());
        CloseHandle(h);
        return false;
    }

    if (verOut.Version.Major != SUDOVDA::VDAProtocolVersion.Major ||
        verOut.Version.Minor < SUDOVDA::VDAProtocolVersion.Minor) {
        errorOut = "Incompatible SudoVDA protocol version";
        CloseHandle(h);
        return false;
    }

    handle_ = h;
    return true;
}

void SudoVdaManager::close() {
    stopWatchdog();
    removeAllManagedMonitors();

    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool SudoVdaManager::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handle_ != INVALID_HANDLE_VALUE;
}

bool SudoVdaManager::detectHighPerformanceRenderAdapter(std::string& gpuNameOut, LUID& luidOut) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT idx = 0;
         SUCCEEDED(factory->EnumAdapterByGpuPreference(idx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
         ++idx) {
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) {
            adapter.Reset();
            continue;
        }
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            adapter.Reset();
            continue;
        }
        std::wstring wdesc(desc.Description);
        if (wdesc.find(L"spacedesk") != std::wstring::npos || wdesc.find(L"SudoMaker") != std::wstring::npos) {
            adapter.Reset();
            continue;
        }
        gpuNameOut = wideToUtf8(wdesc);
        luidOut = desc.AdapterLuid;
        return true;
    }
    return false;
}

bool SudoVdaManager::bindHighPerformanceRenderAdapter(std::string& gpuNameOut, LUID& luidOut) {
    std::string detectedName;
    LUID detectedLuid{};
    if (!detectHighPerformanceRenderAdapter(detectedName, detectedLuid)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    SUDOVDA::VIRTUAL_DISPLAY_SET_RENDER_ADAPTER_PARAMS params{};
    params.AdapterLuid = detectedLuid;
    DWORD returned = 0;
    if (DeviceIoControl(handle_, IOCTL_SET_RENDER_ADAPTER, &params, sizeof(params), nullptr, 0, &returned, nullptr)) {
        gpuNameOut = detectedName;
        luidOut = detectedLuid;
        return true;
    }
    return false;
}

std::optional<VirtualMonitorHandle> SudoVdaManager::addMonitor(
    const std::string& deviceId,
    const std::string& deviceName,
    uint32_t width,
    uint32_t height,
    uint32_t refreshHz,
    std::string& errorOut) {
    const GUID guid = deriveDeviceGuid(deviceId);

    SUDOVDA::VIRTUAL_DISPLAY_ADD_PARAMS params{};
    params.Width = std::clamp(width, 640u, 7680u);
    params.Height = std::clamp(height, 480u, 4320u);
    params.RefreshRate = std::clamp(refreshHz, 30u, 240u) * 1000u; // SudoVDA expects millihertz
    params.MonitorGuid = guid;
    copyAscii13(params.DeviceName, deviceName);
    copyAscii13(params.SerialNumber, deviceId);

    SUDOVDA::VIRTUAL_DISPLAY_ADD_OUT addOut{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_ == INVALID_HANDLE_VALUE) {
            errorOut = "SudoVDA driver handle is not open";
            return std::nullopt;
        }
        DWORD returned = 0;
        if (!DeviceIoControl(handle_, IOCTL_ADD_VIRTUAL_DISPLAY, &params, sizeof(params), &addOut, sizeof(addOut), &returned, nullptr)) {
            errorOut = "IOCTL_ADD_VIRTUAL_DISPLAY failed, win32=" + std::to_string(GetLastError());
            return std::nullopt;
        }
        activeMonitorGuid_ = guid;
        hasActiveMonitor_ = true;
    }

    VirtualMonitorHandle vm{};
    vm.guid = guid;
    vm.adapterLuid = addOut.AdapterLuid;
    vm.targetId = addOut.TargetId;
    vm.width = params.Width;
    vm.height = params.Height;
    vm.refreshHz = std::clamp(refreshHz, 30u, 240u);

    // Wait up to 3 seconds for Windows DisplayConfig / PnP to attach the indirect display path
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (findDisplayByAdapterAndTarget(addOut.AdapterLuid, addOut.TargetId, vm.gdiDeviceName, vm.hMonitor, vm.monitorRect)) {
            break;
        }
        Sleep(100);
    }

    if (vm.gdiDeviceName.empty()) {
        errorOut = "Virtual monitor plugged in, but Windows DisplayConfig path did not become active in time";
        removeMonitor(guid);
        return std::nullopt;
    }

    applyMonitorMode(vm.gdiDeviceName, vm.width, vm.height, vm.refreshHz);
    refreshMonitorRect(vm);
    return vm;
}

bool SudoVdaManager::removeMonitor(const GUID& guid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    SUDOVDA::VIRTUAL_DISPLAY_REMOVE_PARAMS params{};
    params.MonitorGuid = guid;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(handle_, IOCTL_REMOVE_VIRTUAL_DISPLAY, &params, sizeof(params), nullptr, 0, &returned, nullptr);
    if (hasActiveMonitor_ && std::memcmp(&activeMonitorGuid_, &guid, sizeof(GUID)) == 0) {
        hasActiveMonitor_ = false;
    }
    return ok != FALSE;
}

void SudoVdaManager::removeAllManagedMonitors() {
    GUID guidCopy{};
    bool removeNeeded = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hasActiveMonitor_ && handle_ != INVALID_HANDLE_VALUE) {
            guidCopy = activeMonitorGuid_;
            removeNeeded = true;
            hasActiveMonitor_ = false;
        }
    }
    if (removeNeeded) {
        removeMonitor(guidCopy);
    }
}

bool SudoVdaManager::startWatchdog(std::function<void()> onWatchdogFailure) {
    stopWatchdog();

    uint32_t sleepMs = 750;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        SUDOVDA::VIRTUAL_DISPLAY_GET_WATCHDOG_OUT out{};
        DWORD returned = 0;
        if (DeviceIoControl(handle_, IOCTL_GET_WATCHDOG, nullptr, 0, &out, sizeof(out), &returned, nullptr)) {
            if (out.Timeout == 0) {
                return true; // watchdog disabled in registry
            }
            sleepMs = std::clamp(out.Timeout * 1000u / 3u, 200u, 1500u);
        }
    }

    watchdogRunning_.store(true);
    watchdogThread_ = std::thread([this, sleepMs, cb = std::move(onWatchdogFailure)]() {
        uint32_t consecutiveFails = 0;
        while (watchdogRunning_.load()) {
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (handle_ != INVALID_HANDLE_VALUE) {
                    DWORD returned = 0;
                    ok = DeviceIoControl(handle_, IOCTL_DRIVER_PING, nullptr, 0, nullptr, 0, &returned, nullptr) != FALSE;
                }
            }
            if (!ok) {
                ++consecutiveFails;
                if (consecutiveFails > 3 && cb) {
                    cb();
                    break;
                }
            } else {
                consecutiveFails = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    });
    return true;
}

void SudoVdaManager::stopWatchdog() {
    watchdogRunning_.store(false);
    if (watchdogThread_.joinable()) {
        watchdogThread_.join();
    }
}

bool SudoVdaManager::findDisplayByAdapterAndTarget(
    const LUID& adapterLuid,
    uint32_t targetId,
    std::wstring& outGdiName,
    HMONITOR& outHMonitor,
    RECT& outRect) const {
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return false;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
        return false;
    }

    for (const auto& p : paths) {
        if (p.targetInfo.id == targetId &&
            p.targetInfo.adapterId.LowPart == adapterLuid.LowPart &&
            p.targetInfo.adapterId.HighPart == adapterLuid.HighPart) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME srcName{};
            srcName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            srcName.header.size = sizeof(srcName);
            srcName.header.adapterId = p.sourceInfo.adapterId;
            srcName.header.id = p.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&srcName.header) == ERROR_SUCCESS) {
                outGdiName = srcName.viewGdiDeviceName;
                break;
            }
        }
    }

    if (outGdiName.empty()) {
        return false;
    }

    struct SearchCtx {
        const std::wstring* targetName;
        HMONITOR found{nullptr};
        RECT rect{};
    } ctx{&outGdiName};

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<SearchCtx*>(lParam);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            if (*c->targetName == mi.szDevice) {
                c->found = hMon;
                c->rect = mi.rcMonitor;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.found != nullptr) {
        outHMonitor = ctx.found;
        outRect = ctx.rect;
        return true;
    }
    return false;
}

bool SudoVdaManager::applyMonitorMode(const std::wstring& gdiName, uint32_t width, uint32_t height, uint32_t refreshHz) const {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(gdiName.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return false;
    }
    if (dm.dmPelsWidth == width && dm.dmPelsHeight == height && dm.dmDisplayFrequency == refreshHz) {
        return true;
    }
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;
    dm.dmDisplayFrequency = refreshHz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    return ChangeDisplaySettingsExW(gdiName.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) == DISP_CHANGE_SUCCESSFUL;
}

bool SudoVdaManager::refreshMonitorRect(VirtualMonitorHandle& mon) {
    if (mon.gdiDeviceName.empty()) return false;
    struct Ctx {
        const std::wstring* name;
        HMONITOR hMon{nullptr};
        RECT rc{};
        bool ok{false};
    } ctx{&mon.gdiDeviceName};

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lParam);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi) && *c->name == mi.szDevice) {
            c->hMon = hMon;
            c->rc = mi.rcMonitor;
            c->ok = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.ok) {
        mon.hMonitor = ctx.hMon;
        mon.monitorRect = ctx.rc;
    }
    return ctx.ok;
}

bool SudoVdaManager::applyMonitorPlacement(VirtualMonitorHandle& mon, MonitorPlacement placement) {
    if (mon.gdiDeviceName.empty()) return false;

    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mon.gdiDeviceName.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return false;
    }

    struct Bounds {
        int minLeft{0};
        int maxRight{0};
        int minTop{0};
        int maxBottom{0};
        RECT primary{0, 0, 1920, 1080};
        const std::wstring* selfName;
    } b{0, 0, 0, 0, {0, 0, 1920, 1080}, &mon.gdiDeviceName};

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto* ctx = reinterpret_cast<Bounds*>(lParam);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            if (*ctx->selfName != mi.szDevice) {
                if (mi.dwFlags & MONITORINFOF_PRIMARY) {
                    ctx->primary = mi.rcMonitor;
                }
                ctx->minLeft = std::min<int>(ctx->minLeft, mi.rcMonitor.left);
                ctx->maxRight = std::max<int>(ctx->maxRight, mi.rcMonitor.right);
                ctx->minTop = std::min<int>(ctx->minTop, mi.rcMonitor.top);
                ctx->maxBottom = std::max<int>(ctx->maxBottom, mi.rcMonitor.bottom);
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&b));

    const int w = static_cast<int>(dm.dmPelsWidth);
    const int h = static_cast<int>(dm.dmPelsHeight);
    POINTL pos{b.maxRight, b.primary.top};
    switch (placement) {
    case MonitorPlacement::Right:
        pos.x = b.maxRight;
        pos.y = b.primary.top;
        break;
    case MonitorPlacement::Left:
        pos.x = b.minLeft - w;
        pos.y = b.primary.top;
        break;
    case MonitorPlacement::Top:
        pos.x = b.primary.left;
        pos.y = b.minTop - h;
        break;
    case MonitorPlacement::Bottom:
        pos.x = b.primary.left;
        pos.y = b.maxBottom;
        break;
    }

    if (dm.dmPosition.x == pos.x && dm.dmPosition.y == pos.y) {
        return refreshMonitorRect(mon);
    }

    dm.dmPosition = pos;
    dm.dmFields = DM_POSITION;
    LONG r1 = ChangeDisplaySettingsExW(
        mon.gdiDeviceName.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    if (r1 != DISP_CHANGE_SUCCESSFUL) {
        return false;
    }
    LONG r2 = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    if (r2 != DISP_CHANGE_SUCCESSFUL) {
        return false;
    }
    Sleep(80);
    return refreshMonitorRect(mon);
}

} // namespace aegis::vdisplay
