#include "network_server.hpp"
#include "dxgi_capture.hpp"

#include <iphlpapi.h>
#include <qos2.h>
#include <random>
#include <sstream>

namespace aegis::net {

namespace {

bool recvExact(SOCKET s, void* buf, size_t len) {
    auto* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        int n = recv(s, p + got, static_cast<int>(len - got), 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool sendAll(SOCKET s, const void* buf, size_t len) {
    const auto* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, p + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string getComputerNameUtf8() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD sz = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buf, &sz)) {
        int need = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(sz), nullptr, 0, nullptr, nullptr);
        if (need > 0) {
            std::string s(need, '\0');
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(sz), s.data(), need, nullptr, nullptr);
            return s;
        }
    }
    return "aegisDisplay-Host";
}

std::vector<sockaddr_in> enumerateDirectedBroadcastTargets(uint16_t port) {
    std::vector<sockaddr_in> targets;
    ULONG bufLen = 16384;
    std::vector<uint8_t> buf(bufLen);
    auto* addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ULONG res = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, addrs, &bufLen);
    if (res == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufLen);
        addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
        res = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, addrs, &bufLen);
    }
    if (res == NO_ERROR) {
        for (auto* a = addrs; a != nullptr; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* ua = a->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
                if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET) continue;
                auto* sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                uint32_t ip = ntohl(sin->sin_addr.s_addr);
                uint8_t prefix = ua->OnLinkPrefixLength;
                if (prefix == 0 || prefix > 32) continue;
                uint32_t mask = prefix == 32 ? 0xFFFFFFFFu : (~0u << (32u - prefix));
                uint32_t bcast = ip | (~mask);
                sockaddr_in target{};
                target.sin_family = AF_INET;
                target.sin_port = htons(port);
                target.sin_addr.s_addr = htonl(bcast);
                targets.push_back(target);
            }
        }
    }
    sockaddr_in fallback{};
    fallback.sin_family = AF_INET;
    fallback.sin_port = htons(port);
    fallback.sin_addr.s_addr = INADDR_BROADCAST;
    targets.push_back(fallback);
    return targets;
}

void enableSocketLowLatency(SOCKET s, bool isTcp) {
    if (isTcp) {
        BOOL noDelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    }
    int tos = 0xB8; // EF / DSCP 46
    setsockopt(s, IPPROTO_IP, IP_TOS, reinterpret_cast<const char*>(&tos), sizeof(tos));
    int sndBuf = 8 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sndBuf), sizeof(sndBuf));
    int rcvBuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
}

} // namespace

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(
    uint16_t discoveryPort,
    uint16_t controlPort,
    uint16_t videoPort,
    const std::string& gpuName,
    ServerCallbacks callbacks,
    std::string& errorOut) {
    stop();

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        errorOut = "WSAStartup failed";
        return false;
    }
    wsaInit_ = true;

    discoveryPort_ = discoveryPort;
    controlPort_ = controlPort;
    videoPort_ = videoPort;
    gpuName_ = gpuName;
    callbacks_ = std::move(callbacks);
    hostName_ = getComputerNameUtf8();

    std::random_device rd;
    std::ostringstream uid;
    uid << std::hex << rd() << "-" << rd();
    hostUuid_ = uid.str();

    // TCP Control socket
    listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock_ == INVALID_SOCKET) {
        errorOut = "TCP listen socket creation failed";
        stop();
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in tcpAddr{};
    tcpAddr.sin_family = AF_INET;
    tcpAddr.sin_addr.s_addr = INADDR_ANY;
    tcpAddr.sin_port = htons(controlPort_);
    if (bind(listenSock_, reinterpret_cast<sockaddr*>(&tcpAddr), sizeof(tcpAddr)) == SOCKET_ERROR ||
        listen(listenSock_, 4) == SOCKET_ERROR) {
        errorOut = "TCP bind/listen failed on port " + std::to_string(controlPort_) + " wsa=" + std::to_string(WSAGetLastError());
        stop();
        return false;
    }

    // UDP Discovery socket
    discoverySock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discoverySock_ != INVALID_SOCKET) {
        setsockopt(discoverySock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        BOOL bcast = TRUE;
        setsockopt(discoverySock_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bcast), sizeof(bcast));
        sockaddr_in dAddr{};
        dAddr.sin_family = AF_INET;
        dAddr.sin_addr.s_addr = INADDR_ANY;
        dAddr.sin_port = htons(discoveryPort_);
        if (bind(discoverySock_, reinterpret_cast<sockaddr*>(&dAddr), sizeof(dAddr)) == SOCKET_ERROR) {
            closesocket(discoverySock_);
            discoverySock_ = INVALID_SOCKET;
        } else {
            DWORD timeout = 500;
            setsockopt(discoverySock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        }
    }

    // UDP Video socket
    videoUdpSock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (videoUdpSock_ == INVALID_SOCKET) {
        errorOut = "UDP video socket creation failed";
        stop();
        return false;
    }
    enableSocketLowLatency(videoUdpSock_, false);
    sockaddr_in vAddr{};
    vAddr.sin_family = AF_INET;
    vAddr.sin_addr.s_addr = INADDR_ANY;
    vAddr.sin_port = htons(videoPort_);
    if (bind(videoUdpSock_, reinterpret_cast<sockaddr*>(&vAddr), sizeof(vAddr)) == SOCKET_ERROR) {
        errorOut = "UDP video bind failed on port " + std::to_string(videoPort_);
        stop();
        return false;
    }
    DWORD timeout = 500;
    setsockopt(videoUdpSock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    running_.store(true);
    acceptThread_ = std::thread(&NetworkServer::acceptLoop, this);
    udpRxThread_ = std::thread(&NetworkServer::udpVideoRxLoop, this);
    if (discoverySock_ != INVALID_SOCKET) {
        discoveryThread_ = std::thread(&NetworkServer::discoveryLoop, this);
    }
    return true;
}

void NetworkServer::stop() {
    running_.store(false);
    disconnectActiveClient(0);

    if (listenSock_ != INVALID_SOCKET) {
        closesocket(listenSock_);
        listenSock_ = INVALID_SOCKET;
    }
    if (discoverySock_ != INVALID_SOCKET) {
        closesocket(discoverySock_);
        discoverySock_ = INVALID_SOCKET;
    }
    if (videoUdpSock_ != INVALID_SOCKET) {
        closesocket(videoUdpSock_);
        videoUdpSock_ = INVALID_SOCKET;
    }

    if (acceptThread_.joinable()) acceptThread_.join();
    if (udpRxThread_.joinable()) udpRxThread_.join();
    if (discoveryThread_.joinable()) discoveryThread_.join();

    if (wsaInit_) {
        WSACleanup();
        wsaInit_ = false;
    }
}

bool NetworkServer::hasActiveClient() const {
    return activeSessionToken_ != 0;
}

bool NetworkServer::isUdpVideoReady() const {
    return hasClientUdpAddr_;
}

std::string NetworkServer::buildDiscoveryBeaconJson() const {
    std::ostringstream oss;
    oss << "{"
        << "\"magic\":\"ADSP1\","
        << "\"hostId\":\"" << hostUuid_ << "\","
        << "\"hostname\":\"" << hostName_ << "\","
        << "\"tcpPort\":" << controlPort_ << ","
        << "\"udpPort\":" << videoPort_ << ","
        << "\"gpu\":\"" << gpuName_ << "\","
        << "\"codecs\":[\"HEVC\",\"H264\"]"
        << "}";
    return oss.str();
}

void NetworkServer::discoveryLoop() {
    uint64_t lastBeaconUs = 0;
    while (running_.load()) {
        const uint64_t now = capture::nowMicrosecondsQpc();
        if (now - lastBeaconUs >= 1500000ULL) {
            lastBeaconUs = now;
            const std::string beacon = buildDiscoveryBeaconJson();
            for (const auto& target : enumerateDirectedBroadcastTargets(discoveryPort_)) {
                sendto(
                    discoverySock_,
                    beacon.data(),
                    static_cast<int>(beacon.size()),
                    0,
                    reinterpret_cast<const sockaddr*>(&target),
                    sizeof(target));
            }
        }

        char rx[256]{};
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(discoverySock_, rx, sizeof(rx) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            std::string req(rx, n);
            if (req.find("ADSP_DISC") != std::string::npos) {
                const std::string reply = buildDiscoveryBeaconJson();
                sendto(
                    discoverySock_,
                    reply.data(),
                    static_cast<int>(reply.size()),
                    0,
                    reinterpret_cast<const sockaddr*>(&from),
                    fromLen);
            }
        }
    }
}

void NetworkServer::udpVideoRxLoop() {
    while (running_.load()) {
        uint8_t buf[64]{};
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(videoUdpSock_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n >= static_cast<int>(sizeof(adsp::UdpRegisterPacket))) {
            adsp::UdpRegisterPacket pkt{};
            std::memcpy(&pkt, buf, sizeof(pkt));
            if (pkt.magic == adsp::kUdpRegMagic) {
                std::lock_guard<std::mutex> lock(clientMutex_);
                if (activeSessionToken_ != 0 && pkt.sessionToken == activeSessionToken_) {
                    clientUdpAddr_ = from;
                    hasClientUdpAddr_ = true;
                }
            }
        }
    }
}

void NetworkServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in cliAddr{};
        int len = sizeof(cliAddr);
        SOCKET client = accept(listenSock_, reinterpret_cast<sockaddr*>(&cliAddr), &len);
        if (client == INVALID_SOCKET) {
            if (!running_.load()) break;
            Sleep(50);
            continue;
        }

        char ipStr[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &cliAddr.sin_addr, ipStr, sizeof(ipStr));
        enableSocketLowLatency(client, true);

        // Single active primary streaming session at a time (replace previous if reconnecting)
        disconnectActiveClient(1);
        handleClientSocket(client, std::string(ipStr));
    }
}

void NetworkServer::disconnectActiveClient(uint32_t reasonCode) {
    SOCKET sockCopy = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        sockCopy = activeClientSock_;
        activeClientSock_ = INVALID_SOCKET;
        activeSessionToken_ = 0;
        hasClientUdpAddr_ = false;
    }
    if (sockCopy != INVALID_SOCKET) {
        auto pkt = adsp::buildControlPacket(adsp::MsgType::Disconnect, txSeq_.fetch_add(1), &reasonCode, sizeof(reasonCode));
        sendAll(sockCopy, pkt.data(), pkt.size());
        shutdown(sockCopy, SD_BOTH);
        closesocket(sockCopy);
    }
}

bool NetworkServer::sendControlBytes(SOCKET sock, const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (sock == INVALID_SOCKET || sock != activeClientSock_) return false;
    return sendAll(sock, data, len);
}

void NetworkServer::handleClientSocket(SOCKET sock, const std::string& remoteIp) {
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        activeClientSock_ = sock;
        hasClientUdpAddr_ = false;
    }

    QOS_VERSION qosVer{1, 0};
    HANDLE qosHandle = nullptr;
    QOS_FLOWID qosFlow = 0;
    if (QOSCreateHandle(&qosVer, &qosHandle)) {
        QOSAddSocketToFlow(qosHandle, sock, nullptr, QOSTrafficTypeAudioVideo, QOS_NON_ADAPTIVE_FLOW, &qosFlow);
    }

    while (running_.load()) {
        adsp::AdspHeader hdr{};
        if (!recvExact(sock, &hdr, sizeof(hdr))) break;
        if (!adsp::validateHeader(hdr)) break;

        std::vector<uint8_t> payload(hdr.payloadLen);
        if (hdr.payloadLen > 0 && !recvExact(sock, payload.data(), hdr.payloadLen)) {
            break;
        }

        const auto msgType = static_cast<adsp::MsgType>(hdr.msgType);
        switch (msgType) {
        case adsp::MsgType::Hello: {
            if (payload.size() < sizeof(adsp::HelloPayload)) break;
            adsp::HelloPayload hello{};
            std::memcpy(&hello, payload.data(), sizeof(hello));

            ClientSessionInfo info{};
            info.deviceId = adsp::safeFixedCString(hello.deviceId, sizeof(hello.deviceId));
            info.deviceName = adsp::safeFixedCString(hello.deviceName, sizeof(hello.deviceName));
            info.remoteIp = remoteIp;
            info.width = hello.width;
            info.height = hello.height;
            info.refreshHz = hello.refreshHz;
            info.dpi = hello.dpi;
            info.codec = (hello.codecMask & static_cast<uint8_t>(adsp::CodecMask::Hevc)) ? adsp::Codec::Hevc : adsp::Codec::H264;
            info.videoOverTcp = (hello.transportPref == static_cast<uint8_t>(adsp::TransportPref::TcpVideo));

            std::random_device rd;
            uint32_t token = rd();
            if (token == 0) token = 0xAE615001u;
            info.sessionToken = token;

            {
                std::lock_guard<std::mutex> lock(clientMutex_);
                activeSessionToken_ = token;
            }

            adsp::HelloAckPayload ack{};
            if (callbacks_.onClientHello) {
                ack = callbacks_.onClientHello(info);
            } else {
                ack.status = 0;
                ack.sessionToken = token;
                ack.negotiatedCodec = static_cast<uint8_t>(info.codec);
                ack.videoOverTcp = info.videoOverTcp ? 1 : 0;
                ack.udpVideoPort = videoPort_;
                ack.width = info.width;
                ack.height = info.height;
                ack.refreshHz = info.refreshHz;
            }
            auto pkt = adsp::buildControlPacket(adsp::MsgType::HelloAck, txSeq_.fetch_add(1), &ack, sizeof(ack));
            sendControlBytes(sock, pkt.data(), pkt.size());
            break;
        }
        case adsp::MsgType::DisplayConfigReq: {
            if (payload.size() < sizeof(adsp::DisplayConfigReqPayload)) break;
            adsp::DisplayConfigReqPayload req{};
            std::memcpy(&req, payload.data(), sizeof(req));
            adsp::DisplayConfigAckPayload ack{};
            if (callbacks_.onDisplayConfig) {
                ack = callbacks_.onDisplayConfig(req);
            }
            auto pkt = adsp::buildControlPacket(adsp::MsgType::DisplayConfigAck, txSeq_.fetch_add(1), &ack, sizeof(ack));
            sendControlBytes(sock, pkt.data(), pkt.size());
            break;
        }
        case adsp::MsgType::Ping: {
            if (payload.size() >= sizeof(uint64_t)) {
                auto pkt = adsp::buildControlPacket(adsp::MsgType::Pong, txSeq_.fetch_add(1), payload.data(), sizeof(uint64_t));
                sendControlBytes(sock, pkt.data(), pkt.size());
            }
            break;
        }
        case adsp::MsgType::Pong: {
            if (payload.size() >= sizeof(uint64_t) && callbacks_.onRttSample) {
                uint64_t sentUs = 0;
                std::memcpy(&sentUs, payload.data(), sizeof(sentUs));
                const uint64_t nowUs = capture::nowMicrosecondsQpc();
                if (nowUs >= sentUs) {
                    callbacks_.onRttSample(static_cast<double>(nowUs - sentUs) / 1000.0);
                }
            }
            break;
        }
        case adsp::MsgType::InputTouchBatch: {
            if (payload.size() >= 4 && callbacks_.onTouchBatch) {
                uint8_t count = payload[0];
                const size_t expected = 4u + static_cast<size_t>(count) * sizeof(adsp::TouchContact);
                if (count > 0 && payload.size() >= expected) {
                    std::vector<adsp::TouchContact> contacts(count);
                    std::memcpy(contacts.data(), payload.data() + 4, count * sizeof(adsp::TouchContact));
                    callbacks_.onTouchBatch(contacts);
                }
            }
            break;
        }
        case adsp::MsgType::InputPen: {
            if (payload.size() >= sizeof(adsp::InputPenPayload) && callbacks_.onPen) {
                adsp::InputPenPayload pen{};
                std::memcpy(&pen, payload.data(), sizeof(pen));
                callbacks_.onPen(pen);
            }
            break;
        }
        case adsp::MsgType::InputMouse: {
            if (payload.size() >= sizeof(adsp::InputMousePayload) && callbacks_.onMouse) {
                adsp::InputMousePayload mouse{};
                std::memcpy(&mouse, payload.data(), sizeof(mouse));
                callbacks_.onMouse(mouse);
            }
            break;
        }
        case adsp::MsgType::InputKey: {
            if (payload.size() >= sizeof(adsp::InputKeyPayload) && callbacks_.onKey) {
                adsp::InputKeyPayload key{};
                std::memcpy(&key, payload.data(), sizeof(key));
                callbacks_.onKey(key);
            }
            break;
        }
        case adsp::MsgType::FrameAck: {
            if (payload.size() >= sizeof(adsp::FrameAckPayload) && callbacks_.onFrameAck) {
                adsp::FrameAckPayload fa{};
                std::memcpy(&fa, payload.data(), sizeof(fa));
                callbacks_.onFrameAck(fa);
            }
            break;
        }
        case adsp::MsgType::KeyframeReq: {
            if (callbacks_.onKeyframeReq) callbacks_.onKeyframeReq();
            break;
        }
        case adsp::MsgType::Disconnect: {
            goto done;
        }
        default:
            break;
        }
    }

done:
    if (qosHandle) {
        if (qosFlow != 0) QOSRemoveSocketFromFlow(qosHandle, 0, qosFlow, 0);
        QOSCloseHandle(qosHandle);
    }

    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (activeClientSock_ == sock) {
            activeClientSock_ = INVALID_SOCKET;
            activeSessionToken_ = 0;
            hasClientUdpAddr_ = false;
        }
    }
    closesocket(sock);
    if (callbacks_.onClientDisconnected) {
        callbacks_.onClientDisconnected();
    }
}

void NetworkServer::setUdpOptions(uint32_t maxPayloadBytes, bool pacingEnabled) {
    udpPayloadBytes_.store(std::clamp(maxPayloadBytes, 800u, 1420u));
    udpPacingEnabled_.store(pacingEnabled);
}

bool NetworkServer::sendVideoFrame(
    uint32_t sessionToken,
    uint32_t frameId,
    uint64_t ptsUs,
    adsp::Codec codec,
    bool isIdr,
    bool containsCsd,
    bool preferTcp,
    const uint8_t* data,
    size_t size) {
    if (!data || size == 0) return false;

    sockaddr_in udpTarget{};
    bool canUdp = false;
    SOCKET tcpSock = INVALID_SOCKET;

    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (activeSessionToken_ != sessionToken || activeClientSock_ == INVALID_SOCKET) {
            return false;
        }
        canUdp = (!preferTcp && hasClientUdpAddr_ && videoUdpSock_ != INVALID_SOCKET);
        udpTarget = clientUdpAddr_;
        tcpSock = activeClientSock_;
    }

    if (canUdp) {
        const size_t mtu = udpPayloadBytes_.load();
        const bool pace = udpPacingEnabled_.load();
        auto frags = adsp::fragmentVideoFrame(
            sessionToken,
            frameId,
            ptsUs,
            codec,
            isIdr,
            containsCsd,
            data,
            size,
            mtu);
        size_t idx = 0;
        for (const auto& pkt : frags) {
            int sent = sendto(
                videoUdpSock_,
                reinterpret_cast<const char*>(pkt.data()),
                static_cast<int>(pkt.size()),
                0,
                reinterpret_cast<const sockaddr*>(&udpTarget),
                sizeof(udpTarget));
            if (sent == SOCKET_ERROR) {
                ++framesDropped_;
                return false;
            }
            ++idx;
            if (pace && frags.size() > 8 && (idx & 7u) == 0 && idx < frags.size()) {
                LARGE_INTEGER freq{}, t0{}, t1{};
                QueryPerformanceFrequency(&freq);
                QueryPerformanceCounter(&t0);
                const LONGLONG waitTicks = (freq.QuadPart * 20LL) / 1000000LL;
                do {
                    YieldProcessor();
                    QueryPerformanceCounter(&t1);
                } while ((t1.QuadPart - t0.QuadPart) < waitTicks);
            }
        }
        return true;
    }

    // TCP Video Fallback / ADB Tunnel Mode: single AvPacketHeader + full frame inside ADSP Control frame 0x20
    std::vector<uint8_t> payload(sizeof(adsp::AvPacketHeader) + size);
    adsp::AvPacketHeader av{};
    av.magic = adsp::kVideoMagic;
    av.codecFlags = static_cast<uint8_t>(codec) & 0x03u;
    if (isIdr) av.codecFlags |= adsp::VideoFlagIdr;
    if (containsCsd) av.codecFlags |= adsp::VideoFlagCsd;
    av.sessionToken = sessionToken;
    av.frameId = frameId;
    av.fragIdx = 0;
    av.fragCnt = 1;
    av.ptsUs = ptsUs;

    std::memcpy(payload.data(), &av, sizeof(av));
    std::memcpy(payload.data() + sizeof(av), data, size);
    auto tcpPkt = adsp::buildControlPacket(
        adsp::MsgType::VideoFrameTcp,
        txSeq_.fetch_add(1),
        payload.data(),
        static_cast<uint32_t>(payload.size()));
    return sendControlBytes(tcpSock, tcpPkt.data(), tcpPkt.size());
}

} // namespace aegis::net
