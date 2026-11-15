#pragma once

#include "adsp_protocol.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aegis::encode {

struct EncoderConfig {
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{60};
    uint32_t bitrateKbps{22000};
    adsp::Codec codec{adsp::Codec::Hevc};
    bool lowLatency{true};
    bool useVbr{true};
    bool enableIntraRefresh{false};
    uint32_t idrIntervalSec{2};
    uint32_t presetLevel{2}; // 1=P1 Fastest, 2=P2 Low Latency, 3=P3 Balanced, 4=P4 Quality
};

struct EncodedPacket {
    std::vector<uint8_t> annexB;
    bool isKeyFrame{false};
    bool containsCsd{false};
    uint32_t encodeTimeUs{0};
};

class IEncoderBackend {
public:
    virtual ~IEncoderBackend() = default;

    virtual bool initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const EncoderConfig& config,
        std::string& errorOut) = 0;

    virtual bool encodeFrame(
        ID3D11Texture2D* bgraVramTex,
        uint64_t ptsUs,
        bool forceIdr,
        EncodedPacket& outPacket,
        std::string& errorOut) = 0;

    virtual bool updateBitrate(uint32_t newBitrateKbps) = 0;
    virtual std::string backendName() const = 0;
    virtual adsp::Codec activeCodec() const = 0;
    virtual void shutdown() = 0;
};

// Probes and initializes best hardware backend: NVENC Direct -> Media Foundation HW MFT.
std::unique_ptr<IEncoderBackend> createBestHardwareEncoder(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const EncoderConfig& config,
    std::string& diagnosticLog);

} // namespace aegis::encode
