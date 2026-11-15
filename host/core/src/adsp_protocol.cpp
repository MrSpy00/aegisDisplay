#include "adsp_protocol.hpp"
#include <algorithm>

namespace aegis::adsp {

std::vector<std::vector<uint8_t>> fragmentVideoFrame(
    uint32_t sessionToken,
    uint32_t frameId,
    uint64_t ptsUs,
    Codec codec,
    bool isIdr,
    bool containsCsd,
    const uint8_t* data,
    size_t dataSize,
    size_t maxChunkSize) {
    std::vector<std::vector<uint8_t>> packets;
    if (!data || dataSize == 0 || maxChunkSize == 0) {
        return packets;
    }

    const size_t totalFrags = (dataSize + maxChunkSize - 1) / maxChunkSize;
    if (totalFrags > 65535) {
        return packets;
    }

    uint8_t flags = static_cast<uint8_t>(codec) & 0x03u;
    if (isIdr) {
        flags |= VideoFlagIdr;
    }
    if (containsCsd) {
        flags |= VideoFlagCsd;
    }

    packets.reserve(totalFrags);
    for (size_t i = 0; i < totalFrags; ++i) {
        const size_t offset = i * maxChunkSize;
        const size_t chunkLen = std::min(maxChunkSize, dataSize - offset);
        std::vector<uint8_t> pkt(sizeof(AvPacketHeader) + chunkLen);

        AvPacketHeader hdr{};
        hdr.magic = kVideoMagic;
        hdr.codecFlags = flags;
        hdr.reserved = 0;
        hdr.sessionToken = sessionToken;
        hdr.frameId = frameId;
        hdr.fragIdx = static_cast<uint16_t>(i);
        hdr.fragCnt = static_cast<uint16_t>(totalFrags);
        hdr.ptsUs = ptsUs;

        std::memcpy(pkt.data(), &hdr, sizeof(AvPacketHeader));
        std::memcpy(pkt.data() + sizeof(AvPacketHeader), data + offset, chunkLen);
        packets.push_back(std::move(pkt));
    }
    return packets;
}

std::vector<uint8_t> buildControlPacket(
    MsgType msgType,
    uint32_t seq,
    const void* payload,
    uint32_t payloadLen,
    uint16_t flags) {
    std::vector<uint8_t> out(sizeof(AdspHeader) + payloadLen);
    AdspHeader hdr{};
    hdr.magic = kControlMagic;
    hdr.version = kProtocolVersion;
    hdr.msgType = static_cast<uint8_t>(msgType);
    hdr.flags = flags;
    hdr.seq = seq;
    hdr.payloadLen = payloadLen;

    std::memcpy(out.data(), &hdr, sizeof(AdspHeader));
    if (payload && payloadLen > 0) {
        std::memcpy(out.data() + sizeof(AdspHeader), payload, payloadLen);
    }
    return out;
}

bool validateHeader(const AdspHeader& h) {
    return h.magic == kControlMagic &&
           h.version == kProtocolVersion &&
           h.payloadLen <= kMaxControlPayload;
}

std::string safeFixedCString(const char* buf, size_t capacity) {
    if (!buf || capacity == 0) {
        return {};
    }
    size_t len = 0;
    while (len < capacity && buf[len] != '\0') {
        ++len;
    }
    return std::string(buf, len);
}

void writeFixedCString(char* dst, size_t capacity, const std::string& src) {
    if (!dst || capacity == 0) {
        return;
    }
    std::memset(dst, 0, capacity);
    const size_t copyLen = std::min(src.size(), capacity - 1);
    std::memcpy(dst, src.data(), copyLen);
}

} // namespace aegis::adsp
