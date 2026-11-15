#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace aegis::adsp {

inline constexpr uint32_t kControlMagic = 0x50534441u; // 'A','D','S','P' LE
inline constexpr uint16_t kVideoMagic   = 0x5641u;     // 'A','V' LE
inline constexpr uint32_t kUdpRegMagic  = 0x49485641u; // 'A','V','H','I' LE
inline constexpr uint8_t  kProtocolVersion = 1;

inline constexpr uint16_t kDefaultDiscoveryPort = 28450;
inline constexpr uint16_t kDefaultControlPort   = 28451;
inline constexpr uint16_t kDefaultVideoPort     = 28452;
inline constexpr size_t   kMaxUdpPayloadBytes   = 1280;
inline constexpr size_t   kMaxControlPayload    = 16u * 1024u * 1024u;

enum class Codec : uint8_t {
    None = 0,
    H264 = 1,
    Hevc = 2,
};

enum class CodecMask : uint8_t {
    H264 = 0x01,
    Hevc = 0x02,
};

enum class TransportPref : uint8_t {
    Auto     = 0,
    UdpVideo = 1,
    TcpVideo = 2,
};

enum class MsgType : uint8_t {
    Hello            = 0x01,
    HelloAck         = 0x02,
    DisplayConfigReq = 0x03,
    DisplayConfigAck = 0x04,
    Ping             = 0x05,
    Pong             = 0x06,
    FrameAck         = 0x07,
    KeyframeReq      = 0x08,
    InputTouchBatch  = 0x10,
    InputPen         = 0x11,
    InputMouse       = 0x12,
    InputKey         = 0x13,
    VideoFrameTcp    = 0x20,
    Disconnect       = 0x7F,
};

enum class TouchAction : uint8_t {
    Down   = 0,
    Move   = 1,
    Up     = 2,
    Cancel = 3,
};

enum class PenAction : uint8_t {
    Down  = 0,
    Move  = 1,
    Up    = 2,
    Hover = 3,
};

enum PenFlag : uint8_t {
    PenFlagInRange   = 0x01,
    PenFlagInContact = 0x02,
    PenFlagBarrel    = 0x04,
    PenFlagEraser    = 0x08,
};

enum MouseFlag : uint16_t {
    MouseMove        = 0x0001,
    MouseLeftDown    = 0x0002,
    MouseLeftUp      = 0x0004,
    MouseRightDown   = 0x0008,
    MouseRightUp     = 0x0010,
    MouseMiddleDown  = 0x0020,
    MouseMiddleUp    = 0x0040,
    MouseWheel       = 0x0080,
    MouseHWheel      = 0x0100,
    MouseRelative    = 0x0200,
};

enum VideoCodecFlag : uint8_t {
    VideoFlagIdr = 0x04,
    VideoFlagCsd = 0x08,
};

#pragma pack(push, 1)

struct AdspHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msgType;
    uint16_t flags;
    uint32_t seq;
    uint32_t payloadLen;
};
static_assert(sizeof(AdspHeader) == 16);

struct HelloPayload {
    char     deviceId[64];
    char     deviceName[64];
    uint32_t width;
    uint32_t height;
    uint32_t refreshHz;
    uint32_t dpi;
    uint8_t  codecMask;
    uint8_t  transportPref;
    uint16_t reserved;
};
static_assert(sizeof(HelloPayload) == 148);

struct HelloAckPayload {
    uint32_t status;
    uint32_t sessionToken;
    uint8_t  negotiatedCodec;
    uint8_t  videoOverTcp;
    uint16_t udpVideoPort;
    uint32_t width;
    uint32_t height;
    uint32_t refreshHz;
    uint32_t reserved;
};
static_assert(sizeof(HelloAckPayload) == 28);

struct DisplayConfigReqPayload {
    uint32_t width;
    uint32_t height;
    uint32_t refreshHz;
    uint32_t bitrateKbps;
    uint8_t  codec;
    uint8_t  scaleMode;
    uint8_t  lowLatency;
    uint8_t  reserved;
    uint32_t reserved2;
};
static_assert(sizeof(DisplayConfigReqPayload) == 24);

struct DisplayConfigAckPayload {
    uint32_t status;
    uint32_t actualWidth;
    uint32_t actualHeight;
    uint32_t actualRefreshHz;
};
static_assert(sizeof(DisplayConfigAckPayload) == 16);

struct TouchContact {
    uint32_t pointerId;
    uint8_t  action;
    uint8_t  reserved;
    uint16_t xNorm;
    uint16_t yNorm;
    uint16_t pressure;
    uint16_t contactSize;
};
static_assert(sizeof(TouchContact) == 14);

struct InputPenPayload {
    uint8_t  action;
    uint8_t  flags;
    uint16_t xNorm;
    uint16_t yNorm;
    uint16_t pressure;
    uint16_t rotation;
    int8_t   tiltX;
    int8_t   tiltY;
    uint32_t reserved;
};
static_assert(sizeof(InputPenPayload) == 16);

struct InputMousePayload {
    uint16_t flags;
    uint16_t reserved;
    uint16_t xNorm;
    uint16_t yNorm;
    int16_t  wheelDelta;
    int16_t  hWheelDelta;
};
static_assert(sizeof(InputMousePayload) == 12);

struct InputKeyPayload {
    uint16_t scanCode;
    uint16_t vkCode;
    uint8_t  isDown;
    uint8_t  isExtended;
    uint16_t reserved;
};
static_assert(sizeof(InputKeyPayload) == 8);

struct FrameAckPayload {
    uint32_t frameId;
    uint64_t recvTimeUs;
    uint32_t decodeUs;
    uint16_t lossPermille;
    uint16_t jitterMs;
};
static_assert(sizeof(FrameAckPayload) == 20);

struct KeyframeReqPayload {
    uint32_t lastGoodFrameId;
    uint32_t reason;
};
static_assert(sizeof(KeyframeReqPayload) == 8);

struct AvPacketHeader {
    uint16_t magic;
    uint8_t  codecFlags;
    uint8_t  reserved;
    uint32_t sessionToken;
    uint32_t frameId;
    uint16_t fragIdx;
    uint16_t fragCnt;
    uint64_t ptsUs;
};
static_assert(sizeof(AvPacketHeader) == 24);

struct UdpRegisterPacket {
    uint32_t magic;
    uint32_t sessionToken;
};
static_assert(sizeof(UdpRegisterPacket) == 8);

#pragma pack(pop)

std::vector<std::vector<uint8_t>> fragmentVideoFrame(
    uint32_t sessionToken,
    uint32_t frameId,
    uint64_t ptsUs,
    Codec codec,
    bool isIdr,
    bool containsCsd,
    const uint8_t* data,
    size_t dataSize,
    size_t maxChunkSize = kMaxUdpPayloadBytes);

std::vector<uint8_t> buildControlPacket(
    MsgType msgType,
    uint32_t seq,
    const void* payload,
    uint32_t payloadLen,
    uint16_t flags = 0);

bool validateHeader(const AdspHeader& h);
std::string safeFixedCString(const char* buf, size_t capacity);
void writeFixedCString(char* dst, size_t capacity, const std::string& src);

} // namespace aegis::adsp
