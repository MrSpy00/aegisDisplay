#include "ipc_server.hpp"
#include <windows.h>
#include <algorithm>
#include <sstream>

namespace aegis::ipc {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) out += ' ';
            else out += static_cast<char>(c);
        }
    }
    return out;
}

std::string extractJsonString(const std::string& body, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return {};
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return {};
    pos = body.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    auto end = body.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return body.substr(pos + 1, end - pos - 1);
}

uint32_t extractJsonUint(const std::string& body, const std::string& key, uint32_t fallback) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return fallback;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return fallback;
    while (pos + 1 < body.size() && (body[pos + 1] == ' ' || body[pos + 1] == '\t')) ++pos;
    try {
        return static_cast<uint32_t>(std::stoul(body.substr(pos + 1)));
    } catch (...) {
        return fallback;
    }
}

bool extractJsonBool(const std::string& body, const std::string& key, bool fallback) {
    const std::string pat = "\"" + key + "\"";
    auto pos = body.find(pat);
    if (pos == std::string::npos) return fallback;
    pos = body.find(':', pos + pat.size());
    if (pos == std::string::npos) return fallback;
    while (pos + 1 < body.size() && (body[pos + 1] == ' ' || body[pos + 1] == '\t')) ++pos;
    if (body.compare(pos + 1, 4, "true") == 0) return true;
    if (body.compare(pos + 1, 5, "false") == 0) return false;
    return fallback;
}

} // namespace

NamedPipeIpcServer::~NamedPipeIpcServer() {
    stop();
}

bool NamedPipeIpcServer::start(IpcCommandCallbacks callbacks) {
    stop();
    callbacks_ = std::move(callbacks);
    running_.store(true);
    thread_ = std::thread(&NamedPipeIpcServer::serverLoop, this);
    return true;
}

void NamedPipeIpcServer::stop() {
    running_.store(false);
    // Poke the pipe to unblock ConnectNamedPipe
    HANDLE wake = CreateFileW(L"\\\\.\\pipe\\aegisDisplayIpcV1", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
        CloseHandle(wake);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void NamedPipeIpcServer::updateSnapshot(const TelemetrySnapshot& snap) {
    std::lock_guard<std::mutex> lock(snapMutex_);
    snapshot_ = snap;
}

std::string NamedPipeIpcServer::serializeSnapshotJson() const {
    std::lock_guard<std::mutex> lock(snapMutex_);
    std::ostringstream oss;
    oss << "{"
        << "\"state\":\"" << jsonEscape(snapshot_.state) << "\","
        << "\"gpuName\":\"" << jsonEscape(snapshot_.gpuName) << "\","
        << "\"encoderName\":\"" << jsonEscape(snapshot_.encoderName) << "\","
        << "\"clientId\":\"" << jsonEscape(snapshot_.clientId) << "\","
        << "\"clientName\":\"" << jsonEscape(snapshot_.clientName) << "\","
        << "\"clientIp\":\"" << jsonEscape(snapshot_.clientIp) << "\","
        << "\"transportMode\":\"" << jsonEscape(snapshot_.transportMode) << "\","
        << "\"driverInstalled\":" << (snapshot_.driverInstalled ? "true" : "false") << ","
        << "\"virtualDisplayActive\":" << (snapshot_.virtualDisplayActive ? "true" : "false") << ","
        << "\"width\":" << snapshot_.width << ","
        << "\"height\":" << snapshot_.height << ","
        << "\"refreshHz\":" << snapshot_.refreshHz << ","
        << "\"targetBitrateKbps\":" << snapshot_.targetBitrateKbps << ","
        << "\"currentFps\":" << snapshot_.currentFps << ","
        << "\"encodeMs\":" << snapshot_.encodeMs << ","
        << "\"rttMs\":" << snapshot_.rttMs << ","
        << "\"lossPermille\":" << snapshot_.lossPermille << ","
        << "\"framesSent\":" << snapshot_.framesSent << ","
        << "\"framesDropped\":" << snapshot_.framesDropped << ","
        << "\"cursorEnabled\":" << (snapshot_.cursorEnabled ? "true" : "false") << ","
        << "\"useVbr\":" << (snapshot_.useVbr ? "true" : "false") << ","
        << "\"presetLevel\":" << snapshot_.presetLevel << ","
        << "\"idrIntervalSec\":" << snapshot_.idrIntervalSec << ","
        << "\"intraRefresh\":" << (snapshot_.intraRefresh ? "true" : "false") << ","
        << "\"codecOverride\":\"" << jsonEscape(snapshot_.codecOverride) << "\","
        << "\"maxFps\":" << snapshot_.maxFps << ","
        << "\"monitorPlacement\":\"" << jsonEscape(snapshot_.monitorPlacement) << "\","
        << "\"udpPayloadBytes\":" << snapshot_.udpPayloadBytes << ","
        << "\"udpPacingEnabled\":" << (snapshot_.udpPacingEnabled ? "true" : "false")
        << "}\n";
    return oss.str();
}

void NamedPipeIpcServer::handleCommandJson(const std::string& json) {
    const std::string cmd = extractJsonString(json, "cmd");
    if (cmd == "set_bitrate" && callbacks_.onSetBitrate) {
        const uint32_t kbps = extractJsonUint(json, "kbps", 22000);
        callbacks_.onSetBitrate(kbps);
    } else if (cmd == "apply_settings" && callbacks_.onApplySettings) {
        HostSettingsUpdate upd{};
        {
            std::lock_guard<std::mutex> lock(snapMutex_);
            upd.bitrateKbps = snapshot_.targetBitrateKbps;
            upd.cursorEnabled = snapshot_.cursorEnabled;
            upd.useVbr = snapshot_.useVbr;
            upd.presetLevel = snapshot_.presetLevel;
            upd.idrIntervalSec = snapshot_.idrIntervalSec;
            upd.intraRefresh = snapshot_.intraRefresh;
            upd.codecOverride = snapshot_.codecOverride;
            upd.maxFps = snapshot_.maxFps;
            upd.monitorPlacement = snapshot_.monitorPlacement;
            upd.udpPayloadBytes = snapshot_.udpPayloadBytes;
            upd.udpPacingEnabled = snapshot_.udpPacingEnabled;
        }
        upd.bitrateKbps = std::clamp(extractJsonUint(json, "bitrateKbps", upd.bitrateKbps), 2000u, 80000u);
        upd.cursorEnabled = extractJsonBool(json, "cursorEnabled", upd.cursorEnabled);
        upd.useVbr = extractJsonBool(json, "useVbr", upd.useVbr);
        upd.presetLevel = std::clamp(extractJsonUint(json, "presetLevel", upd.presetLevel), 1u, 4u);
        upd.idrIntervalSec = std::clamp(extractJsonUint(json, "idrIntervalSec", upd.idrIntervalSec), 1u, 10u);
        upd.intraRefresh = extractJsonBool(json, "intraRefresh", upd.intraRefresh);
        const std::string co = extractJsonString(json, "codecOverride");
        if (!co.empty()) upd.codecOverride = co;
        upd.maxFps = std::clamp(extractJsonUint(json, "maxFps", upd.maxFps), 30u, 240u);
        const std::string mp = extractJsonString(json, "monitorPlacement");
        if (!mp.empty()) upd.monitorPlacement = mp;
        upd.udpPayloadBytes = std::clamp(extractJsonUint(json, "udpPayloadBytes", upd.udpPayloadBytes), 800u, 1420u);
        upd.udpPacingEnabled = extractJsonBool(json, "udpPacingEnabled", upd.udpPacingEnabled);
        callbacks_.onApplySettings(upd);
    } else if (cmd == "keyframe" && callbacks_.onRequestKeyframe) {
        callbacks_.onRequestKeyframe();
    } else if (cmd == "disconnect" && callbacks_.onDisconnectClient) {
        callbacks_.onDisconnectClient();
    } else if (cmd == "shutdown" && callbacks_.onShutdown) {
        callbacks_.onShutdown();
    }
}

void NamedPipeIpcServer::serverLoop() {
    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\aegisDisplayIpcV1",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            4,
            16384,
            4096,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }

        BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!running_.load()) {
            CloseHandle(pipe);
            break;
        }

        if (connected) {
            char buf[4096]{};
            DWORD bytesRead = 0;
            if (ReadFile(pipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                std::string req(buf, bytesRead);
                handleCommandJson(req);
            }
            const std::string reply = serializeSnapshotJson();
            DWORD written = 0;
            WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr);
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
}

} // namespace aegis::ipc
