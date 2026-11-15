#pragma once

#include "adsp_protocol.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aegis::net {

struct ClientSessionInfo {
    uint32_t sessionToken{0};
    std::string deviceId;
    std::string deviceName;
    std::string remoteIp;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t refreshHz{60};
    uint32_t dpi{320};
    adsp::Codec codec{adsp::Codec::Hevc};
    bool videoOverTcp{false};
};

struct ServerCallbacks {
    std::function<adsp::HelloAckPayload(const ClientSessionInfo&)> onClientHello;
    std::function<adsp::DisplayConfigAckPayload(const adsp::DisplayConfigReqPayload&)> onDisplayConfig;
    std::function<void(const std::vector<adsp::TouchContact>&)> onTouchBatch;
    std::function<void(const adsp::InputPenPayload&)> onPen;
    std::function<void(const adsp::InputMousePayload&)> onMouse;
    std::function<void(const adsp::InputKeyPayload&)> onKey;
    std::function<void(const adsp::FrameAckPayload&)> onFrameAck;
    std::function<void()> onKeyframeReq;
    std::function<void(double rttMs)> onRttSample;
    std::function<void()> onClientDisconnected;
};

class NetworkServer {
public:
    NetworkServer() = default;
    ~NetworkServer();

    bool start(
        uint16_t discoveryPort,
        uint16_t controlPort,
        uint16_t videoPort,
        const std::string& gpuName,
        ServerCallbacks callbacks,
        std::string& errorOut);

    void stop();

    bool hasActiveClient() const;
    bool isUdpVideoReady() const;
    void setUdpOptions(uint32_t maxPayloadBytes, bool pacingEnabled);
    uint64_t framesDropped() const { return framesDropped_.load(); }

    bool sendVideoFrame(
        uint32_t sessionToken,
        uint32_t frameId,
        uint64_t ptsUs,
        adsp::Codec codec,
        bool isIdr,
        bool containsCsd,
        bool preferTcp,
        const uint8_t* data,
        size_t size);

    void disconnectActiveClient(uint32_t reasonCode = 0);

private:
    void discoveryLoop();
    void acceptLoop();
    void udpVideoRxLoop();
    void handleClientSocket(SOCKET sock, const std::string& remoteIp);
    bool sendControlBytes(SOCKET sock, const void* data, size_t len);
    std::string buildDiscoveryBeaconJson() const;

    uint16_t discoveryPort_{adsp::kDefaultDiscoveryPort};
    uint16_t controlPort_{adsp::kDefaultControlPort};
    uint16_t videoPort_{adsp::kDefaultVideoPort};
    std::string gpuName_;
    std::string hostUuid_;
    std::string hostName_;
    ServerCallbacks callbacks_;

    bool wsaInit_{false};
    SOCKET listenSock_{INVALID_SOCKET};
    SOCKET discoverySock_{INVALID_SOCKET};
    SOCKET videoUdpSock_{INVALID_SOCKET};

    std::mutex clientMutex_;
    SOCKET activeClientSock_{INVALID_SOCKET};
    uint32_t activeSessionToken_{0};
    sockaddr_in clientUdpAddr_{};
    bool hasClientUdpAddr_{false};
    std::atomic<uint32_t> txSeq_{1};
    std::atomic<uint32_t> udpPayloadBytes_{static_cast<uint32_t>(adsp::kMaxUdpPayloadBytes)};
    std::atomic<bool> udpPacingEnabled_{true};
    std::atomic<uint64_t> framesDropped_{0};

    std::atomic<bool> running_{false};
    std::thread discoveryThread_;
    std::thread acceptThread_;
    std::thread udpRxThread_;
};

} // namespace aegis::net
