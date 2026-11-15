#include "adsp_protocol.hpp"
#include "dxgi_capture.hpp"
#include "input_injector.hpp"
#include "ipc_server.hpp"
#include "network_server.hpp"
#include "sudovda_manager.hpp"
#include "video_encoder.hpp"

#include <avrt.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <objbase.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> gStopRequested{false};

std::filesystem::path getHostConfigPath() {
    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::filesystem::path base = (n > 0 && n < MAX_PATH) ? std::filesystem::path(appData) : std::filesystem::current_path();
    return base / L"aegisSoft" / L"aegisDisplay" / L"host_config.json";
}

struct HostSettings {
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

std::string jsonReadStr(const std::string& body, const std::string& key, const std::string& fb) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return fb;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return fb;
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) return fb;
    auto end = body.find('"', pos + 1);
    if (end == std::string::npos) return fb;
    return body.substr(pos + 1, end - pos - 1);
}

uint32_t jsonReadUint(const std::string& body, const std::string& key, uint32_t fb) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return fb;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return fb;
    while (pos + 1 < body.size() && (body[pos + 1] == ' ' || body[pos + 1] == '\t')) ++pos;
    try {
        return static_cast<uint32_t>(std::stoul(body.substr(pos + 1)));
    } catch (...) {
        return fb;
    }
}

bool jsonReadBool(const std::string& body, const std::string& key, bool fb) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return fb;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return fb;
    while (pos + 1 < body.size() && (body[pos + 1] == ' ' || body[pos + 1] == '\t')) ++pos;
    if (body.compare(pos + 1, 4, "true") == 0) return true;
    if (body.compare(pos + 1, 5, "false") == 0) return false;
    return fb;
}

HostSettings loadHostSettings() {
    HostSettings s{};
    std::ifstream in(getHostConfigPath());
    if (!in.is_open()) return s;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string body = ss.str();
    s.bitrateKbps = std::clamp(jsonReadUint(body, "bitrateKbps", s.bitrateKbps), 2000u, 80000u);
    s.cursorEnabled = jsonReadBool(body, "cursorEnabled", s.cursorEnabled);
    s.useVbr = jsonReadBool(body, "useVbr", s.useVbr);
    s.presetLevel = std::clamp(jsonReadUint(body, "presetLevel", s.presetLevel), 1u, 4u);
    s.idrIntervalSec = std::clamp(jsonReadUint(body, "idrIntervalSec", s.idrIntervalSec), 1u, 10u);
    s.intraRefresh = jsonReadBool(body, "intraRefresh", s.intraRefresh);
    s.codecOverride = jsonReadStr(body, "codecOverride", s.codecOverride);
    s.maxFps = std::clamp(jsonReadUint(body, "maxFps", s.maxFps), 30u, 240u);
    s.monitorPlacement = jsonReadStr(body, "monitorPlacement", s.monitorPlacement);
    s.udpPayloadBytes = std::clamp(jsonReadUint(body, "udpPayloadBytes", s.udpPayloadBytes), 800u, 1420u);
    s.udpPacingEnabled = jsonReadBool(body, "udpPacingEnabled", s.udpPacingEnabled);
    return s;
}

void saveHostSettings(const HostSettings& s) {
    std::error_code ec;
    const auto path = getHostConfigPath();
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;
    out << "{\n"
        << "  \"bitrateKbps\": " << s.bitrateKbps << ",\n"
        << "  \"cursorEnabled\": " << (s.cursorEnabled ? "true" : "false") << ",\n"
        << "  \"useVbr\": " << (s.useVbr ? "true" : "false") << ",\n"
        << "  \"presetLevel\": " << s.presetLevel << ",\n"
        << "  \"idrIntervalSec\": " << s.idrIntervalSec << ",\n"
        << "  \"intraRefresh\": " << (s.intraRefresh ? "true" : "false") << ",\n"
        << "  \"codecOverride\": \"" << s.codecOverride << "\",\n"
        << "  \"maxFps\": " << s.maxFps << ",\n"
        << "  \"monitorPlacement\": \"" << s.monitorPlacement << "\",\n"
        << "  \"udpPayloadBytes\": " << s.udpPayloadBytes << ",\n"
        << "  \"udpPacingEnabled\": " << (s.udpPacingEnabled ? "true" : "false") << "\n"
        << "}\n";
}

aegis::vdisplay::MonitorPlacement parsePlacement(const std::string& p) {
    if (p == "LEFT") return aegis::vdisplay::MonitorPlacement::Left;
    if (p == "TOP") return aegis::vdisplay::MonitorPlacement::Top;
    if (p == "BOTTOM") return aegis::vdisplay::MonitorPlacement::Bottom;
    return aegis::vdisplay::MonitorPlacement::Right;
}

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        gStopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

std::filesystem::path findSudoVdaDriverDir(const wchar_t* argv0) {
    std::error_code ec;
    auto exeDir = std::filesystem::weakly_canonical(argv0, ec).parent_path();
    const std::filesystem::path candidates[] = {
        exeDir / L"drivers" / L"sudovda",
        exeDir.parent_path() / L"drivers" / L"sudovda",
        exeDir.parent_path().parent_path() / L"drivers" / L"sudovda",
        exeDir.parent_path().parent_path().parent_path() / L"drivers" / L"sudovda",
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c / L"SudoVDA.inf", ec)) {
            return c;
        }
    }
    return exeDir / L"drivers" / L"sudovda";
}

struct ActiveStreamState {
    bool active{false};
    uint32_t sessionToken{0};
    std::string deviceId;
    std::string deviceName;
    std::string remoteIp;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t refreshHz{60};
    aegis::adsp::Codec clientCodec{aegis::adsp::Codec::Hevc};
    bool videoOverTcp{false};
    bool forceKeyframe{false};
    bool reconfigureRequested{false};
    bool placementUpdateRequested{false};
    HostSettings hostSettings{};
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    std::wstring mode = L"--daemon";
    if (argc > 1 && argv[1]) {
        mode = argv[1];
    }

    if (mode == L"--install-driver") {
        auto dir = findSudoVdaDriverDir(argv[0]);
        std::string err;
        if (aegis::vdisplay::SudoVdaManager::installDriver(dir, err)) {
            std::cout << "[OK] SudoVDA driver installed from: " << dir.string() << "\n";
            CoUninitialize();
            return 0;
        }
        std::cerr << "[FAIL] SudoVDA driver install failed: " << err << "\n";
        CoUninitialize();
        return 1;
    }

    if (mode == L"--uninstall-driver") {
        std::string err;
        if (aegis::vdisplay::SudoVdaManager::uninstallDriver(err)) {
            std::cout << "[OK] SudoVDA virtual display adapter removed.\n";
            CoUninitialize();
            return 0;
        }
        std::cerr << "[FAIL] " << err << "\n";
        CoUninitialize();
        return 1;
    }

    aegis::vdisplay::SudoVdaManager vda;
    std::string gpuName = "Microsoft Basic Render";
    LUID gpuLuid{};
    aegis::vdisplay::SudoVdaManager::detectHighPerformanceRenderAdapter(gpuName, gpuLuid);
    std::string err;

    const bool driverPresent = aegis::vdisplay::SudoVdaManager::isDriverInstalled();
    bool vdaOpen = false;
    if (driverPresent && vda.open(err)) {
        vdaOpen = true;
        vda.bindHighPerformanceRenderAdapter(gpuName, gpuLuid);
        vda.startWatchdog([&]() {
            std::cerr << "[WARN] SudoVDA watchdog communication lost.\n";
        });
    } else if (driverPresent) {
        std::cerr << "[WARN] SudoVDA open warning: " << err << "\n";
    }

    if (mode == L"--probe") {
        std::cout << "driver_installed=" << (driverPresent ? "true" : "false") << "\n";
        std::cout << "driver_open=" << (vdaOpen ? "true" : "false") << "\n";
        std::cout << "gpu=" << gpuName << "\n";

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            Microsoft::WRL::ComPtr<IDXGIAdapter> cand;
            for (UINT i = 0; factory->EnumAdapters(i, &cand) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC d{};
                if (SUCCEEDED(cand->GetDesc(&d)) &&
                    d.AdapterLuid.LowPart == gpuLuid.LowPart &&
                    d.AdapterLuid.HighPart == gpuLuid.HighPart) {
                    adapter = cand;
                    break;
                }
                cand.Reset();
            }
        }
        Microsoft::WRL::ComPtr<ID3D11Device> dev;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL achieved{};
        HRESULT hr = D3D11CreateDevice(
            adapter.Get(),
            adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels,
            2,
            D3D11_SDK_VERSION,
            &dev,
            &achieved,
            &ctx);
        if (SUCCEEDED(hr) && dev && ctx) {
            aegis::encode::EncoderConfig cfg;
            cfg.width = 1920;
            cfg.height = 1080;
            cfg.fps = 60;
            cfg.bitrateKbps = 18000;
            cfg.codec = aegis::adsp::Codec::Hevc;
            std::string diag;
            auto enc = aegis::encode::createBestHardwareEncoder(dev.Get(), ctx.Get(), cfg, diag);
            std::cout << "encoder_diag=" << diag << "\n";
            if (enc) {
                std::cout << "encoder_backend=" << enc->backendName() << "\n";
                D3D11_TEXTURE2D_DESC td{};
                td.Width = cfg.width;
                td.Height = cfg.height;
                td.MipLevels = 1;
                td.ArraySize = 1;
                td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)) && tex) {
                    aegis::encode::EncodedPacket pkt;
                    std::string encErr;
                    if (enc->encodeFrame(tex.Get(), 16666, true, pkt, encErr)) {
                        std::cout << "encoder_test_frame=OK bytes=" << pkt.annexB.size()
                                  << " idr=" << (pkt.isKeyFrame ? "true" : "false")
                                  << " encode_us=" << pkt.encodeTimeUs << "\n";
                    } else {
                        std::cout << "encoder_test_frame=FAIL err=" << encErr << "\n";
                    }
                }
                enc->shutdown();
            } else {
                std::cout << "encoder_backend=NONE\n";
            }
        } else {
            std::cout << "encoder_backend=D3D11_FAIL\n";
        }

        CoUninitialize();
        return 0;
    }

    if (mode == L"--test-vdisplay") {
        if (!vdaOpen) {
            std::cerr << "[FAIL] SudoVDA not open/installed: " << err << "\n";
            CoUninitialize();
            return 2;
        }
        auto mon = vda.addMonitor("test-probe-01", "aegisTest", 1920, 1080, 60, err);
        if (!mon) {
            std::cerr << "[FAIL] addMonitor failed: " << err << "\n";
            CoUninitialize();
            return 3;
        }
        std::wcout << L"[OK] Virtual display attached: " << mon->gdiDeviceName
                   << L" rect=(" << mon->monitorRect.left << L"," << mon->monitorRect.top
                   << L"," << mon->monitorRect.right << L"," << mon->monitorRect.bottom << L")\n";
        Sleep(250);
        aegis::capture::DxgiDuplicator dup;
        if (dup.initialize(mon->adapterLuid, mon->gdiDeviceName, err)) {
            dup.setMonitorRect(mon->monitorRect);
            dup.setCursorEnabled(true);
            std::cout << "[OK] Capture adapter: " << dup.adapterDescription()
                      << " (" << dup.width() << "x" << dup.height() << ")\n";
            aegis::encode::EncoderConfig ecfg{};
            ecfg.width = dup.width();
            ecfg.height = dup.height();
            ecfg.fps = 60;
            ecfg.bitrateKbps = 22000;
            ecfg.codec = aegis::adsp::Codec::Hevc;
            std::string diag;
            auto enc = aegis::encode::createBestHardwareEncoder(dup.device(), dup.context(), ecfg, diag);
            std::cout << "[OK] Encoder: " << diag << "\n";
            if (enc) {
                for (int i = 0; i < 3; ++i) {
                    aegis::capture::CapturedFrame cf{};
                    bool lost = false;
                    std::string ce;
                    if (dup.acquireFrame(200, cf, lost, ce)) {
                        aegis::encode::EncodedPacket pkt{};
                        if (enc->encodeFrame(cf.gpuBgraTexture.Get(), cf.captureTimestampUs, i == 0, pkt, ce)) {
                            std::cout << "  frame " << i << ": bytes=" << pkt.annexB.size()
                                      << " idr=" << pkt.isKeyFrame << " enc_us=" << pkt.encodeTimeUs << "\n";
                        }
                    }
                }
                enc->shutdown();
            }
            dup.shutdown();
        } else {
            std::cerr << "[WARN] Capture init on vdisplay failed: " << err << "\n";
        }
        vda.removeMonitor(mon->guid);
        std::cout << "[OK] Virtual display cleanly removed.\n";
        vda.close();
        CoUninitialize();
        return 0;
    }

    // --daemon interactive core mode
    aegis::input::InputInjector injector;
    injector.initialize();

    std::mutex stateMtx;
    ActiveStreamState streamState;
    streamState.hostSettings = loadHostSettings();

    aegis::ipc::TelemetrySnapshot snap;
    snap.gpuName = gpuName;
    snap.driverInstalled = driverPresent;
    snap.targetBitrateKbps = streamState.hostSettings.bitrateKbps;
    snap.cursorEnabled = streamState.hostSettings.cursorEnabled;
    snap.useVbr = streamState.hostSettings.useVbr;
    snap.presetLevel = streamState.hostSettings.presetLevel;
    snap.idrIntervalSec = streamState.hostSettings.idrIntervalSec;
    snap.intraRefresh = streamState.hostSettings.intraRefresh;
    snap.codecOverride = streamState.hostSettings.codecOverride;
    snap.maxFps = streamState.hostSettings.maxFps;
    snap.monitorPlacement = streamState.hostSettings.monitorPlacement;
    snap.udpPayloadBytes = streamState.hostSettings.udpPayloadBytes;
    snap.udpPacingEnabled = streamState.hostSettings.udpPacingEnabled;

    aegis::ipc::NamedPipeIpcServer ipc;
    aegis::net::NetworkServer net;
    net.setUdpOptions(streamState.hostSettings.udpPayloadBytes, streamState.hostSettings.udpPacingEnabled);

    auto syncSnapshotSettingsLocked = [&]() {
        const auto& hs = streamState.hostSettings;
        snap.targetBitrateKbps = hs.bitrateKbps;
        snap.cursorEnabled = hs.cursorEnabled;
        snap.useVbr = hs.useVbr;
        snap.presetLevel = hs.presetLevel;
        snap.idrIntervalSec = hs.idrIntervalSec;
        snap.intraRefresh = hs.intraRefresh;
        snap.codecOverride = hs.codecOverride;
        snap.maxFps = hs.maxFps;
        snap.monitorPlacement = hs.monitorPlacement;
        snap.udpPayloadBytes = hs.udpPayloadBytes;
        snap.udpPacingEnabled = hs.udpPacingEnabled;
        ipc.updateSnapshot(snap);
    };

    ipc.updateSnapshot(snap);
    ipc.start({
        .onSetBitrate = [&](uint32_t kbps) {
            std::lock_guard<std::mutex> lk(stateMtx);
            streamState.hostSettings.bitrateKbps = std::clamp(kbps, 2000u, 80000u);
            saveHostSettings(streamState.hostSettings);
            syncSnapshotSettingsLocked();
        },
        .onApplySettings = [&](const aegis::ipc::HostSettingsUpdate& upd) {
            std::lock_guard<std::mutex> lk(stateMtx);
            auto& hs = streamState.hostSettings;
            const bool encoderChanged =
                (hs.useVbr != upd.useVbr) ||
                (hs.presetLevel != upd.presetLevel) ||
                (hs.idrIntervalSec != upd.idrIntervalSec) ||
                (hs.intraRefresh != upd.intraRefresh) ||
                (hs.codecOverride != upd.codecOverride) ||
                (hs.maxFps != upd.maxFps);
            const bool placementChanged = (hs.monitorPlacement != upd.monitorPlacement);

            hs.bitrateKbps = std::clamp(upd.bitrateKbps, 2000u, 80000u);
            hs.cursorEnabled = upd.cursorEnabled;
            hs.useVbr = upd.useVbr;
            hs.presetLevel = std::clamp(upd.presetLevel, 1u, 4u);
            hs.idrIntervalSec = std::clamp(upd.idrIntervalSec, 1u, 10u);
            hs.intraRefresh = upd.intraRefresh;
            hs.codecOverride = upd.codecOverride;
            hs.maxFps = std::clamp(upd.maxFps, 30u, 240u);
            hs.monitorPlacement = upd.monitorPlacement;
            hs.udpPayloadBytes = std::clamp(upd.udpPayloadBytes, 800u, 1420u);
            hs.udpPacingEnabled = upd.udpPacingEnabled;

            net.setUdpOptions(hs.udpPayloadBytes, hs.udpPacingEnabled);
            saveHostSettings(hs);
            if (encoderChanged && streamState.active) {
                streamState.reconfigureRequested = true;
            }
            if (placementChanged && streamState.active) {
                streamState.placementUpdateRequested = true;
            }
            streamState.forceKeyframe = true;
            syncSnapshotSettingsLocked();
        },
        .onRequestKeyframe = [&]() {
            std::lock_guard<std::mutex> lk(stateMtx);
            streamState.forceKeyframe = true;
        },
        .onDisconnectClient = [&]() {
            net.disconnectActiveClient(2);
        },
        .onShutdown = [&]() {
            gStopRequested.store(true);
        }
    });

    auto resolveCodec = [](aegis::adsp::Codec clientPref, const std::string& overrideStr) -> aegis::adsp::Codec {
        if (overrideStr == "HEVC") return aegis::adsp::Codec::Hevc;
        if (overrideStr == "H264") return aegis::adsp::Codec::H264;
        return clientPref;
    };

    aegis::net::ServerCallbacks cbs{};
    cbs.onClientHello = [&](const aegis::net::ClientSessionInfo& info) -> aegis::adsp::HelloAckPayload {
        std::lock_guard<std::mutex> lk(stateMtx);
        streamState.active = true;
        streamState.sessionToken = info.sessionToken;
        streamState.deviceId = info.deviceId;
        streamState.deviceName = info.deviceName;
        streamState.remoteIp = info.remoteIp;
        streamState.width = std::clamp(info.width, 640u, 3840u);
        streamState.height = std::clamp(info.height, 480u, 2560u);
        streamState.refreshHz = std::clamp(info.refreshHz, 30u, 120u);
        streamState.clientCodec = info.codec;
        const auto negotiated = resolveCodec(info.codec, streamState.hostSettings.codecOverride);
        streamState.videoOverTcp = info.videoOverTcp;
        streamState.reconfigureRequested = true;
        streamState.forceKeyframe = true;

        snap.state = "STREAMING";
        snap.clientId = info.deviceId;
        snap.clientName = info.deviceName;
        snap.clientIp = info.remoteIp;
        snap.width = streamState.width;
        snap.height = streamState.height;
        snap.refreshHz = std::min(streamState.refreshHz, streamState.hostSettings.maxFps);
        snap.transportMode = info.videoOverTcp ? "TCP_TUNNEL" : "UDP_LOW_LATENCY";
        ipc.updateSnapshot(snap);

        aegis::adsp::HelloAckPayload ack{};
        ack.status = 0;
        ack.sessionToken = info.sessionToken;
        ack.negotiatedCodec = static_cast<uint8_t>(negotiated);
        ack.videoOverTcp = info.videoOverTcp ? 1 : 0;
        ack.udpVideoPort = aegis::adsp::kDefaultVideoPort;
        ack.width = streamState.width;
        ack.height = streamState.height;
        ack.refreshHz = std::min(streamState.refreshHz, streamState.hostSettings.maxFps);
        return ack;
    };

    cbs.onDisplayConfig = [&](const aegis::adsp::DisplayConfigReqPayload& req) -> aegis::adsp::DisplayConfigAckPayload {
        std::lock_guard<std::mutex> lk(stateMtx);
        const bool sameDims =
            (req.width == streamState.width) &&
            (req.height == streamState.height) &&
            (req.refreshHz == streamState.refreshHz) &&
            (req.codec == 0 || static_cast<aegis::adsp::Codec>(req.codec) == streamState.clientCodec);

        streamState.width = std::clamp(req.width, 640u, 3840u);
        streamState.height = std::clamp(req.height, 480u, 2560u);
        streamState.refreshHz = std::clamp(req.refreshHz, 30u, 120u);
        if (req.bitrateKbps >= 2000u) {
            streamState.hostSettings.bitrateKbps = std::clamp(req.bitrateKbps, 2000u, 80000u);
            saveHostSettings(streamState.hostSettings);
        }
        if (req.codec == static_cast<uint8_t>(aegis::adsp::Codec::H264) ||
            req.codec == static_cast<uint8_t>(aegis::adsp::Codec::Hevc)) {
            streamState.clientCodec = static_cast<aegis::adsp::Codec>(req.codec);
        }
        if (!sameDims) {
            streamState.reconfigureRequested = true;
        }
        streamState.forceKeyframe = true;
        syncSnapshotSettingsLocked();

        aegis::adsp::DisplayConfigAckPayload ack{};
        ack.status = 0;
        ack.actualWidth = streamState.width;
        ack.actualHeight = streamState.height;
        ack.actualRefreshHz = std::min(streamState.refreshHz, streamState.hostSettings.maxFps);
        return ack;
    };

    cbs.onTouchBatch = [&](const std::vector<aegis::adsp::TouchContact>& contacts) {
        injector.injectTouchBatch(contacts);
    };
    cbs.onPen = [&](const aegis::adsp::InputPenPayload& pen) {
        injector.injectPen(pen);
    };
    cbs.onMouse = [&](const aegis::adsp::InputMousePayload& mouse) {
        injector.injectMouse(mouse);
    };
    cbs.onKey = [&](const aegis::adsp::InputKeyPayload& key) {
        injector.injectKey(key);
    };
    cbs.onKeyframeReq = [&]() {
        std::lock_guard<std::mutex> lk(stateMtx);
        streamState.forceKeyframe = true;
    };
    cbs.onFrameAck = [&](const aegis::adsp::FrameAckPayload& fa) {
        std::lock_guard<std::mutex> lk(stateMtx);
        snap.lossPermille = static_cast<double>(fa.lossPermille);
        if (fa.lossPermille > 30 && streamState.hostSettings.bitrateKbps > 6000) {
            streamState.hostSettings.bitrateKbps = static_cast<uint32_t>(streamState.hostSettings.bitrateKbps * 0.85);
            snap.targetBitrateKbps = streamState.hostSettings.bitrateKbps;
        }
    };
    cbs.onRttSample = [&](double rttMs) {
        std::lock_guard<std::mutex> lk(stateMtx);
        snap.rttMs = rttMs;
    };
    cbs.onClientDisconnected = [&]() {
        std::lock_guard<std::mutex> lk(stateMtx);
        streamState.active = false;
        streamState.sessionToken = 0;
        streamState.reconfigureRequested = true;
        snap.state = "IDLE";
        snap.clientId.clear();
        snap.clientName.clear();
        snap.clientIp.clear();
        snap.virtualDisplayActive = false;
        snap.currentFps = 0.0;
        ipc.updateSnapshot(snap);
    };

    if (!net.start(
            aegis::adsp::kDefaultDiscoveryPort,
            aegis::adsp::kDefaultControlPort,
            aegis::adsp::kDefaultVideoPort,
            gpuName,
            std::move(cbs),
            err)) {
        std::cerr << "[FAIL] NetworkServer start failed: " << err << "\n";
        injector.shutdown();
        vda.close();
        CoUninitialize();
        return 4;
    }

    std::cout << "[READY] aegisDisplayCore listening on TCP " << aegis::adsp::kDefaultControlPort
              << " / UDP " << aegis::adsp::kDefaultVideoPort << " (GPU: " << gpuName << ")\n";

    // Dedicated capture & encode thread
    std::thread pipelineThread([&]() {
        DWORD taskIdx = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        std::optional<aegis::vdisplay::VirtualMonitorHandle> currentMon;
        aegis::capture::DxgiDuplicator duplicator;
        std::unique_ptr<aegis::encode::IEncoderBackend> encoder;
        uint32_t appliedBitrate = 22000;
        uint32_t frameId = 1;
        uint64_t fpsWindowStartUs = aegis::capture::nowMicrosecondsQpc();
        uint32_t fpsFrameCount = 0;
        uint64_t lastFrameSentUs = 0;

        while (!gStopRequested.load()) {
            ActiveStreamState local{};
            {
                std::lock_guard<std::mutex> lk(stateMtx);
                local = streamState;
                streamState.forceKeyframe = false;
                streamState.reconfigureRequested = false;
                streamState.placementUpdateRequested = false;
            }

            if (!local.active) {
                if (encoder) {
                    encoder->shutdown();
                    encoder.reset();
                }
                duplicator.shutdown();
                if (currentMon) {
                    vda.removeMonitor(currentMon->guid);
                    currentMon.reset();
                }
                Sleep(30);
                continue;
            }

            if (local.placementUpdateRequested && currentMon) {
                aegis::vdisplay::SudoVdaManager::applyMonitorPlacement(
                    *currentMon, parsePlacement(local.hostSettings.monitorPlacement));
                duplicator.setMonitorRect(currentMon->monitorRect);
                injector.setTargetMonitorRect(currentMon->monitorRect);
            }

            if (local.reconfigureRequested || !encoder) {
                encoder.reset();
                duplicator.shutdown();
                if (local.reconfigureRequested && currentMon) {
                    vda.removeMonitor(currentMon->guid);
                    currentMon.reset();
                }

                std::string stepErr;
                if (!vda.isOpen()) {
                    vda.open(stepErr);
                    if (vda.isOpen()) {
                        vda.bindHighPerformanceRenderAdapter(gpuName, gpuLuid);
                        vda.startWatchdog();
                    }
                }

                const uint32_t effectiveFps = std::clamp(
                    std::min(local.refreshHz, local.hostSettings.maxFps), 30u, 240u);

                std::wstring targetGdi;
                LUID targetLuid = gpuLuid;
                if (vda.isOpen() && !currentMon) {
                    currentMon = vda.addMonitor(
                        local.deviceId,
                        local.deviceName,
                        local.width,
                        local.height,
                        effectiveFps,
                        stepErr);
                    if (currentMon) {
                        aegis::vdisplay::SudoVdaManager::applyMonitorPlacement(
                            *currentMon, parsePlacement(local.hostSettings.monitorPlacement));
                    }
                }
                if (currentMon) {
                    targetGdi = currentMon->gdiDeviceName;
                    targetLuid = currentMon->adapterLuid;
                    injector.setTargetMonitorRect(currentMon->monitorRect);
                }

                if (targetGdi.empty()) {
                    targetGdi = L"\\\\.\\DISPLAY1";
                    RECT prim{0, 0, static_cast<LONG>(GetSystemMetrics(SM_CXSCREEN)), static_cast<LONG>(GetSystemMetrics(SM_CYSCREEN))};
                    injector.setTargetMonitorRect(prim);
                }

                if (duplicator.initialize(targetLuid, targetGdi, stepErr)) {
                    duplicator.setCursorEnabled(local.hostSettings.cursorEnabled);
                    if (currentMon) {
                        duplicator.setMonitorRect(currentMon->monitorRect);
                    }

                    aegis::encode::EncoderConfig ecfg{};
                    ecfg.width = duplicator.width();
                    ecfg.height = duplicator.height();
                    ecfg.fps = effectiveFps;
                    ecfg.bitrateKbps = local.hostSettings.bitrateKbps;
                    ecfg.codec = resolveCodec(local.clientCodec, local.hostSettings.codecOverride);
                    ecfg.lowLatency = true;
                    ecfg.useVbr = local.hostSettings.useVbr;
                    ecfg.enableIntraRefresh = local.hostSettings.intraRefresh;
                    ecfg.idrIntervalSec = local.hostSettings.idrIntervalSec;
                    ecfg.presetLevel = local.hostSettings.presetLevel;

                    std::string encDiag;
                    encoder = aegis::encode::createBestHardwareEncoder(
                        duplicator.device(), duplicator.context(), ecfg, encDiag);
                    if (!encoder && ecfg.codec == aegis::adsp::Codec::Hevc) {
                        ecfg.codec = aegis::adsp::Codec::H264;
                        encoder = aegis::encode::createBestHardwareEncoder(
                            duplicator.device(), duplicator.context(), ecfg, encDiag);
                    }
                    appliedBitrate = local.hostSettings.bitrateKbps;

                    std::lock_guard<std::mutex> lk(stateMtx);
                    snap.virtualDisplayActive = currentMon.has_value();
                    snap.encoderName = encoder ? encoder->backendName() : "FAILED";
                    snap.width = duplicator.width();
                    snap.height = duplicator.height();
                    snap.refreshHz = effectiveFps;
                    ipc.updateSnapshot(snap);
                    std::cout << "[STREAM] Capture=" << duplicator.adapterDescription()
                              << " | " << encDiag
                              << " (" << duplicator.width() << "x" << duplicator.height()
                              << "@" << effectiveFps << "Hz)\n";
                } else {
                    std::cerr << "[WARN] Duplicator init failed: " << stepErr << "\n";
                    Sleep(250);
                    continue;
                }
            }

            if (duplicator.cursorEnabled() != local.hostSettings.cursorEnabled) {
                duplicator.setCursorEnabled(local.hostSettings.cursorEnabled);
            }

            if (encoder && local.hostSettings.bitrateKbps != appliedBitrate) {
                if (encoder->updateBitrate(local.hostSettings.bitrateKbps)) {
                    appliedBitrate = local.hostSettings.bitrateKbps;
                }
            }

            const uint32_t effectiveFps = std::clamp(
                std::min(local.refreshHz, local.hostSettings.maxFps), 30u, 240u);
            const uint64_t frameBudgetUs = 1000000ULL / effectiveFps;
            const uint64_t nowBeforeAcq = aegis::capture::nowMicrosecondsQpc();
            if (lastFrameSentUs > 0 && nowBeforeAcq < lastFrameSentUs + frameBudgetUs) {
                const uint64_t remainUs = (lastFrameSentUs + frameBudgetUs) - nowBeforeAcq;
                if (remainUs > 2000ULL) {
                    Sleep(static_cast<DWORD>(remainUs / 2000ULL));
                }
            }

            aegis::capture::CapturedFrame capFrame{};
            bool accessLost = false;
            std::string capErr;
            if (duplicator.acquireFrame(16, capFrame, accessLost, capErr)) {
                lastFrameSentUs = aegis::capture::nowMicrosecondsQpc();
                aegis::encode::EncodedPacket pkt{};
                std::string encErr;
                if (encoder && encoder->encodeFrame(
                        capFrame.gpuBgraTexture.Get(),
                        capFrame.captureTimestampUs,
                        local.forceKeyframe,
                        pkt,
                        encErr)) {
                    net.sendVideoFrame(
                        local.sessionToken,
                        frameId++,
                        capFrame.captureTimestampUs,
                        encoder->activeCodec(),
                        pkt.isKeyFrame,
                        pkt.containsCsd,
                        local.videoOverTcp || !net.isUdpVideoReady(),
                        pkt.annexB.data(),
                        pkt.annexB.size());

                    ++fpsFrameCount;
                    const uint64_t nowUs = aegis::capture::nowMicrosecondsQpc();
                    if (nowUs - fpsWindowStartUs >= 300000ULL) {
                        const double secs = static_cast<double>(nowUs - fpsWindowStartUs) / 1000000.0;
                        std::lock_guard<std::mutex> lk(stateMtx);
                        snap.currentFps = static_cast<double>(fpsFrameCount) / secs;
                        snap.encodeMs = static_cast<double>(pkt.encodeTimeUs) / 1000.0;
                        snap.framesSent = frameId - 1;
                        snap.framesDropped = net.framesDropped();
                        snap.transportMode = (local.videoOverTcp || !net.isUdpVideoReady()) ? "TCP_TUNNEL" : "UDP_LOW_LATENCY";
                        ipc.updateSnapshot(snap);
                        fpsWindowStartUs = nowUs;
                        fpsFrameCount = 0;
                    }
                }
            } else if (accessLost) {
                std::string reErr;
                if (!duplicator.reacquire(reErr)) {
                    Sleep(100);
                }
                if (currentMon) {
                    vda.refreshMonitorRect(*currentMon);
                    duplicator.setMonitorRect(currentMon->monitorRect);
                    injector.setTargetMonitorRect(currentMon->monitorRect);
                }
            }
        }

        if (encoder) encoder->shutdown();
        duplicator.shutdown();
        if (currentMon) vda.removeMonitor(currentMon->guid);
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    });

    while (!gStopRequested.load()) {
        Sleep(100);
    }

    if (pipelineThread.joinable()) pipelineThread.join();
    ipc.stop();
    net.stop();
    injector.shutdown();
    vda.close();
    CoUninitialize();
    return 0;
}
