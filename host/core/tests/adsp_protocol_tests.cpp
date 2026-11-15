#include "adsp_protocol.hpp"
#include "input_math.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

using namespace aegis::adsp;
using namespace aegis::input;

static void testControlPacketRoundTrip() {
    HelloPayload hello{};
    writeFixedCString(hello.deviceId, sizeof(hello.deviceId), "aegis-test-device-01");
    writeFixedCString(hello.deviceName, sizeof(hello.deviceName), "Honor Magic");
    hello.width = 2560;
    hello.height = 1200;
    hello.refreshHz = 120;
    hello.dpi = 480;
    hello.codecMask = static_cast<uint8_t>(CodecMask::Hevc) | static_cast<uint8_t>(CodecMask::H264);
    hello.transportPref = static_cast<uint8_t>(TransportPref::UdpVideo);

    auto bytes = buildControlPacket(MsgType::Hello, 42, &hello, sizeof(hello));
    assert(bytes.size() == sizeof(AdspHeader) + sizeof(HelloPayload));

    AdspHeader hdr{};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    assert(validateHeader(hdr));
    assert(hdr.msgType == static_cast<uint8_t>(MsgType::Hello));
    assert(hdr.seq == 42);
    assert(hdr.payloadLen == sizeof(HelloPayload));

    HelloPayload decoded{};
    std::memcpy(&decoded, bytes.data() + sizeof(AdspHeader), sizeof(decoded));
    assert(safeFixedCString(decoded.deviceId, sizeof(decoded.deviceId)) == "aegis-test-device-01");
    assert(safeFixedCString(decoded.deviceName, sizeof(decoded.deviceName)) == "Honor Magic");
    assert(decoded.width == 2560 && decoded.height == 1200 && decoded.refreshHz == 120);
}

static void testFragmentationAndReassembly() {
    std::vector<uint8_t> frame(4096);
    std::iota(frame.begin(), frame.end(), static_cast<uint8_t>(1));

    auto pkts = fragmentVideoFrame(
        0x11223344u,
        777u,
        9876543210ULL,
        Codec::Hevc,
        true,
        true,
        frame.data(),
        frame.size(),
        kMaxUdpPayloadBytes);

    assert(pkts.size() == 4); // 1360 * 3 = 4080, +16 = 4096

    std::vector<uint8_t> reassembled;
    for (size_t i = 0; i < pkts.size(); ++i) {
        AvPacketHeader hdr{};
        std::memcpy(&hdr, pkts[i].data(), sizeof(hdr));
        assert(hdr.magic == kVideoMagic);
        assert(hdr.sessionToken == 0x11223344u);
        assert(hdr.frameId == 777u);
        assert(hdr.fragIdx == i);
        assert(hdr.fragCnt == pkts.size());
        assert(hdr.ptsUs == 9876543210ULL);
        assert((hdr.codecFlags & 0x03u) == static_cast<uint8_t>(Codec::Hevc));
        assert((hdr.codecFlags & VideoFlagIdr) != 0);
        assert((hdr.codecFlags & VideoFlagCsd) != 0);

        reassembled.insert(reassembled.end(), pkts[i].begin() + sizeof(AvPacketHeader), pkts[i].end());
    }
    assert(reassembled == frame);
}

static void testInputCoordinateMapping() {
    // Extended virtual display positioned to the right of a 2560x1600 primary monitor
    DisplayRect virtualMon{2560, 0, 2560 + 1920, 1080};
    DisplayRect virtualDesktop{0, 0, 2560 + 1920, 1600};

    auto topLeft = mapNormToMonitorPixel(0, 0, virtualMon);
    assert(topLeft.x == 2560 && topLeft.y == 0);

    auto botRight = mapNormToMonitorPixel(65535, 65535, virtualMon);
    assert(botRight.x == 2560 + 1919 && botRight.y == 1079);

    auto mid = mapNormToMonitorPixel(32768, 32768, virtualMon);
    assert(mid.x >= 2560 + 959 && mid.x <= 2560 + 960);
    assert(mid.y >= 539 && mid.y <= 540);

    auto vdPt = mapPixelToVirtualDesk(botRight.x, botRight.y, virtualDesktop);
    assert(vdPt.x == 65535);
    assert(vdPt.y > 40000 && vdPt.y < 46000);
}

int main() {
    testControlPacketRoundTrip();
    testFragmentationAndReassembly();
    testInputCoordinateMapping();
    std::cout << "[PASS] all aegis_protocol_tests passed.\n";
    return 0;
}
