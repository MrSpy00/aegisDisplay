#include "video_encoder.hpp"
#include "dxgi_capture.hpp"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <icodecapi.h>
#include <codecapi.h>
#include <propvarutil.h>

#include <algorithm>
#include <cstring>
#include <sstream>

#include "nvEncodeAPI.h"

using Microsoft::WRL::ComPtr;

namespace aegis::encode {

namespace {

bool annexBContainsIdr(const uint8_t* data, size_t size, adsp::Codec codec) {
    if (!data || size < 5) return false;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0) {
            size_t startLen = 0;
            if (data[i + 2] == 1) startLen = 3;
            else if (i + 5 < size && data[i + 2] == 0 && data[i + 3] == 1) startLen = 4;
            if (startLen > 0) {
                uint8_t nal = data[i + startLen];
                if (codec == adsp::Codec::H264) {
                    uint8_t t = nal & 0x1Fu;
                    if (t == 5 || t == 7) return true;
                } else if (codec == adsp::Codec::Hevc) {
                    uint8_t t = (nal >> 1) & 0x3Fu;
                    if (t == 19 || t == 20 || t == 32) return true;
                }
                i += startLen;
            }
        }
    }
    return false;
}

class NvencDirectEncoder final : public IEncoderBackend {
public:
    ~NvencDirectEncoder() override { shutdown(); }

    bool initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const EncoderConfig& config,
        std::string& errorOut) override {
        shutdown();
        if (!device || !context) {
            errorOut = "Null D3D11 device";
            return false;
        }
        device_ = device;
        context_ = context;
        config_ = config;

        nvencLib_ = LoadLibraryW(L"nvEncodeAPI64.dll");
        if (!nvencLib_) {
            errorOut = "nvEncodeAPI64.dll not present";
            return false;
        }

        using PfnGetMaxVer = NVENCSTATUS(NVENCAPI*)(uint32_t*);
        using PfnCreateInst = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);

        auto pGetMax = reinterpret_cast<PfnGetMaxVer>(GetProcAddress(nvencLib_, "NvEncodeAPIGetMaxSupportedVersion"));
        auto pCreate = reinterpret_cast<PfnCreateInst>(GetProcAddress(nvencLib_, "NvEncodeAPICreateInstance"));
        if (!pGetMax || !pCreate) {
            errorOut = "Missing NvEncodeAPI exports";
            shutdown();
            return false;
        }

        uint32_t maxVer = 0;
        if (pGetMax(&maxVer) != NV_ENC_SUCCESS) {
            errorOut = "NvEncodeAPIGetMaxSupportedVersion failed";
            shutdown();
            return false;
        }
        const uint32_t maxMajor = maxVer >> 4;
        const uint32_t maxMinor = maxVer & 0x0Fu;
        if (maxMajor < NVENCAPI_MAJOR_VERSION ||
            (maxMajor == NVENCAPI_MAJOR_VERSION && maxMinor < NVENCAPI_MINOR_VERSION)) {
            std::ostringstream oss;
            oss << "Installed NVIDIA driver NVENC API (" << maxMajor << "." << maxMinor
                << ") is older than compiled header (" << NVENCAPI_MAJOR_VERSION << "." << NVENCAPI_MINOR_VERSION << ")";
            errorOut = oss.str();
            shutdown();
            return false;
        }

        fn_.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        if (pCreate(&fn_) != NV_ENC_SUCCESS) {
            errorOut = "NvEncodeAPICreateInstance failed";
            shutdown();
            return false;
        }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams{};
        sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        sessionParams.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        sessionParams.device = device_.Get();
        sessionParams.apiVersion = NVENCAPI_VERSION;

        NVENCSTATUS st = fn_.nvEncOpenEncodeSessionEx(&sessionParams, &encoder_);
        if (st != NV_ENC_SUCCESS || !encoder_) {
            std::ostringstream oss;
            oss << "nvEncOpenEncodeSessionEx failed status=" << st;
            errorOut = oss.str();
            shutdown();
            return false;
        }

        const GUID codecGuid = (config_.codec == adsp::Codec::Hevc) ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;
        const GUID presetGuid = [&]() {
            switch (config_.presetLevel) {
            case 1: return NV_ENC_PRESET_P1_GUID;
            case 2: return NV_ENC_PRESET_P2_GUID;
            case 4: return NV_ENC_PRESET_P4_GUID;
            case 3:
            default: return NV_ENC_PRESET_P3_GUID;
            }
        }();
        const NV_ENC_TUNING_INFO tuning = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

        NV_ENC_PRESET_CONFIG presetCfg{};
        presetCfg.version = NV_ENC_PRESET_CONFIG_VER;
        presetCfg.presetCfg.version = NV_ENC_CONFIG_VER;
        st = fn_.nvEncGetEncodePresetConfigEx(encoder_, codecGuid, presetGuid, tuning, &presetCfg);
        if (st != NV_ENC_SUCCESS) {
            errorOut = "nvEncGetEncodePresetConfigEx failed status=" + std::to_string(st);
            shutdown();
            return false;
        }

        encConfig_ = presetCfg.presetCfg;
        encConfig_.version = NV_ENC_CONFIG_VER;
        const uint32_t idrFrames = std::max(1u, config_.fps * std::max(1u, config_.idrIntervalSec));
        encConfig_.gopLength = config_.enableIntraRefresh ? NVENC_INFINITE_GOPLENGTH : idrFrames;
        encConfig_.frameIntervalP = 1; // zero B-frames
        const uint32_t avgBps = config_.bitrateKbps * 1000u;
        const uint32_t maxBps = config_.useVbr ? static_cast<uint32_t>(avgBps * 1.35) : avgBps;
        encConfig_.rcParams.rateControlMode = config_.useVbr ? NV_ENC_PARAMS_RC_VBR : NV_ENC_PARAMS_RC_CBR;
        encConfig_.rcParams.averageBitRate = avgBps;
        encConfig_.rcParams.maxBitRate = maxBps;
        // 2-frame VBV buffer headroom avoids macroblock starvation/tearing on fast screen redraws
        encConfig_.rcParams.vbvBufferSize = std::max(avgBps / 8u, (maxBps * 2u) / std::max(1u, config_.fps));
        encConfig_.rcParams.vbvInitialDelay = encConfig_.rcParams.vbvBufferSize;
        encConfig_.rcParams.zeroReorderDelay = 1;

        if (config_.codec == adsp::Codec::Hevc) {
            encConfig_.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
            encConfig_.encodeCodecConfig.hevcConfig.idrPeriod = config_.enableIntraRefresh ? NVENC_INFINITE_GOPLENGTH : idrFrames;
            encConfig_.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;
            encConfig_.encodeCodecConfig.hevcConfig.enableIntraRefresh = config_.enableIntraRefresh ? 1u : 0u;
            if (config_.enableIntraRefresh) {
                encConfig_.encodeCodecConfig.hevcConfig.intraRefreshPeriod = idrFrames;
                encConfig_.encodeCodecConfig.hevcConfig.intraRefreshCnt = 5;
            }
        } else {
            encConfig_.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
            encConfig_.encodeCodecConfig.h264Config.idrPeriod = config_.enableIntraRefresh ? NVENC_INFINITE_GOPLENGTH : idrFrames;
            encConfig_.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
            encConfig_.encodeCodecConfig.h264Config.enableIntraRefresh = config_.enableIntraRefresh ? 1u : 0u;
            if (config_.enableIntraRefresh) {
                encConfig_.encodeCodecConfig.h264Config.intraRefreshPeriod = idrFrames;
                encConfig_.encodeCodecConfig.h264Config.intraRefreshCnt = 5;
            }
        }

        initParams_ = {};
        initParams_.version = NV_ENC_INITIALIZE_PARAMS_VER;
        initParams_.encodeGUID = codecGuid;
        initParams_.presetGUID = presetGuid;
        initParams_.tuningInfo = tuning;
        initParams_.encodeWidth = config_.width;
        initParams_.encodeHeight = config_.height;
        initParams_.darWidth = config_.width;
        initParams_.darHeight = config_.height;
        initParams_.frameRateNum = config_.fps;
        initParams_.frameRateDen = 1;
        initParams_.enableEncodeAsync = 0;
        initParams_.enablePTD = 1;
        initParams_.encodeConfig = &encConfig_;

        st = fn_.nvEncInitializeEncoder(encoder_, &initParams_);
        if (st != NV_ENC_SUCCESS) {
            errorOut = "nvEncInitializeEncoder failed status=" + std::to_string(st);
            shutdown();
            return false;
        }

        NV_ENC_CREATE_BITSTREAM_BUFFER bsParams{};
        bsParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        st = fn_.nvEncCreateBitstreamBuffer(encoder_, &bsParams);
        if (st != NV_ENC_SUCCESS || !bsParams.bitstreamBuffer) {
            errorOut = "nvEncCreateBitstreamBuffer failed";
            shutdown();
            return false;
        }
        bitstreamBuf_ = bsParams.bitstreamBuffer;
        return true;
    }

    bool encodeFrame(
        ID3D11Texture2D* bgraVramTex,
        uint64_t ptsUs,
        bool forceIdr,
        EncodedPacket& outPacket,
        std::string& errorOut) override {
        if (!encoder_ || !bgraVramTex) {
            errorOut = "NVENC not initialized";
            return false;
        }
        const uint64_t t0 = capture::nowMicrosecondsQpc();

        // Keep persistent registrations for alternating textures (desktop vs cursor-composited)
        NV_ENC_REGISTERED_PTR regHandle = nullptr;
        for (const auto& entry : registeredCache_) {
            if (entry.first == bgraVramTex) {
                regHandle = entry.second;
                break;
            }
        }
        if (!regHandle) {
            D3D11_TEXTURE2D_DESC td{};
            bgraVramTex->GetDesc(&td);

            NV_ENC_REGISTER_RESOURCE reg{};
            reg.version = NV_ENC_REGISTER_RESOURCE_VER;
            reg.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
            reg.width = td.Width;
            reg.height = td.Height;
            reg.pitch = 0;
            reg.resourceToRegister = bgraVramTex;
            reg.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;
            reg.bufferUsage = NV_ENC_INPUT_IMAGE;
            NVENCSTATUS st = fn_.nvEncRegisterResource(encoder_, &reg);
            if (st != NV_ENC_SUCCESS) {
                errorOut = "nvEncRegisterResource failed status=" + std::to_string(st);
                return false;
            }
            regHandle = reg.registeredResource;
            registeredCache_.push_back({bgraVramTex, regHandle});
        }

        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = regHandle;
        NVENCSTATUS st = fn_.nvEncMapInputResource(encoder_, &map);
        if (st != NV_ENC_SUCCESS) {
            errorOut = "nvEncMapInputResource failed status=" + std::to_string(st);
            return false;
        }

        NV_ENC_PIC_PARAMS pic{};
        pic.version = NV_ENC_PIC_PARAMS_VER;
        pic.inputWidth = config_.width;
        pic.inputHeight = config_.height;
        pic.inputPitch = config_.width;
        pic.inputBuffer = map.mappedResource;
        pic.outputBitstream = bitstreamBuf_;
        pic.bufferFmt = map.mappedBufferFmt;
        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        pic.inputTimeStamp = ptsUs;
        if (forceIdr || frameCounter_ == 0) {
            pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
        }

        st = fn_.nvEncEncodePicture(encoder_, &pic);
        if (st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT) {
            fn_.nvEncUnmapInputResource(encoder_, map.mappedResource);
            errorOut = "nvEncEncodePicture failed status=" + std::to_string(st);
            return false;
        }

        NV_ENC_LOCK_BITSTREAM lock{};
        lock.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = bitstreamBuf_;
        st = fn_.nvEncLockBitstream(encoder_, &lock);
        if (st == NV_ENC_SUCCESS && lock.bitstreamSizeInBytes > 0) {
            const auto* bytes = static_cast<const uint8_t*>(lock.bitstreamBufferPtr);
            outPacket.annexB.assign(bytes, bytes + lock.bitstreamSizeInBytes);
            outPacket.isKeyFrame = (lock.pictureType == NV_ENC_PIC_TYPE_IDR || lock.pictureType == NV_ENC_PIC_TYPE_I);
            outPacket.containsCsd = outPacket.isKeyFrame || (frameCounter_ == 0);
            fn_.nvEncUnlockBitstream(encoder_, bitstreamBuf_);
        }
        fn_.nvEncUnmapInputResource(encoder_, map.mappedResource);
        outPacket.encodeTimeUs = static_cast<uint32_t>(capture::nowMicrosecondsQpc() - t0);
        ++frameCounter_;
        return !outPacket.annexB.empty();
    }

    bool updateBitrate(uint32_t newBitrateKbps) override {
        if (!encoder_) return false;
        config_.bitrateKbps = std::clamp(newBitrateKbps, 2000u, 100000u);
        const uint32_t avgBps = config_.bitrateKbps * 1000u;
        const uint32_t maxBps = config_.useVbr ? static_cast<uint32_t>(avgBps * 1.35) : avgBps;
        encConfig_.rcParams.averageBitRate = avgBps;
        encConfig_.rcParams.maxBitRate = maxBps;
        encConfig_.rcParams.vbvBufferSize = std::max(avgBps / 8u, (maxBps * 2u) / std::max(1u, config_.fps));
        encConfig_.rcParams.vbvInitialDelay = encConfig_.rcParams.vbvBufferSize;

        NV_ENC_RECONFIGURE_PARAMS reconf{};
        reconf.version = NV_ENC_RECONFIGURE_PARAMS_VER;
        reconf.reInitEncodeParams = initParams_;
        return fn_.nvEncReconfigureEncoder(encoder_, &reconf) == NV_ENC_SUCCESS;
    }

    std::string backendName() const override {
        return config_.codec == adsp::Codec::Hevc ? "NVENC Direct (HEVC)" : "NVENC Direct (H.264)";
    }

    adsp::Codec activeCodec() const override { return config_.codec; }

    void shutdown() override {
        if (encoder_) {
            for (const auto& entry : registeredCache_) {
                if (entry.second) {
                    fn_.nvEncUnregisterResource(encoder_, entry.second);
                }
            }
            registeredCache_.clear();
            if (bitstreamBuf_) {
                fn_.nvEncDestroyBitstreamBuffer(encoder_, bitstreamBuf_);
                bitstreamBuf_ = nullptr;
            }
            fn_.nvEncDestroyEncoder(encoder_);
            encoder_ = nullptr;
        }
        if (nvencLib_) {
            FreeLibrary(nvencLib_);
            nvencLib_ = nullptr;
        }
    }

private:
    HMODULE nvencLib_{nullptr};
    NV_ENCODE_API_FUNCTION_LIST fn_{};
    void* encoder_{nullptr};
    std::vector<std::pair<ID3D11Texture2D*, NV_ENC_REGISTERED_PTR>> registeredCache_;
    NV_ENC_OUTPUT_PTR bitstreamBuf_{nullptr};
    NV_ENC_CONFIG encConfig_{};
    NV_ENC_INITIALIZE_PARAMS initParams_{};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    EncoderConfig config_{};
    uint64_t frameCounter_{0};
};

// Tier 2 Fallback: Media Foundation Hardware MFT (NVENC MFT / AMD AMF / Intel QSV) with GPU VideoProcessor BGRA->NV12
class MfHardwareEncoder final : public IEncoderBackend {
public:
    ~MfHardwareEncoder() override { shutdown(); }

    bool initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const EncoderConfig& config,
        std::string& errorOut) override {
        shutdown();
        if (!device || !context) {
            errorOut = "Null D3D11 device";
            return false;
        }
        device_ = device;
        context_ = context;
        config_ = config;

        if (FAILED(MFStartup(MF_VERSION))) {
            errorOut = "MFStartup failed";
            return false;
        }
        mfStarted_ = true;

        UINT resetToken = 0;
        if (FAILED(MFCreateDXGIDeviceManager(&resetToken, &dxgiMgr_)) ||
            FAILED(dxgiMgr_->ResetDevice(device_.Get(), resetToken))) {
            errorOut = "MFCreateDXGIDeviceManager failed";
            shutdown();
            return false;
        }

        // VideoProcessor for GPU BGRA -> NV12 conversion
        if (FAILED(device_.As(&videoDev_)) || FAILED(context_.As(&videoCtx_))) {
            errorOut = "D3D11VideoDevice interface unavailable";
            shutdown();
            return false;
        }

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC vpDesc{};
        vpDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        vpDesc.InputWidth = config_.width;
        vpDesc.InputHeight = config_.height;
        vpDesc.OutputWidth = config_.width;
        vpDesc.OutputHeight = config_.height;
        vpDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        if (FAILED(videoDev_->CreateVideoProcessorEnumerator(&vpDesc, &vpEnum_)) ||
            FAILED(videoDev_->CreateVideoProcessor(vpEnum_.Get(), 0, &vp_))) {
            errorOut = "CreateVideoProcessor failed";
            shutdown();
            return false;
        }

        D3D11_TEXTURE2D_DESC nv12Desc{};
        nv12Desc.Width = config_.width;
        nv12Desc.Height = config_.height;
        nv12Desc.MipLevels = 1;
        nv12Desc.ArraySize = 1;
        nv12Desc.Format = DXGI_FORMAT_NV12;
        nv12Desc.SampleDesc.Count = 1;
        nv12Desc.Usage = D3D11_USAGE_DEFAULT;
        nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&nv12Desc, nullptr, &nv12Tex_))) {
            errorOut = "CreateTexture2D NV12 failed";
            shutdown();
            return false;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc{};
        outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (FAILED(videoDev_->CreateVideoProcessorOutputView(nv12Tex_.Get(), vpEnum_.Get(), &outViewDesc, &nv12OutView_))) {
            errorOut = "CreateVideoProcessorOutputView failed";
            shutdown();
            return false;
        }

        MFT_REGISTER_TYPE_INFO outInfo{};
        outInfo.guidMajorType = MFMediaType_Video;
        outInfo.guidSubtype = (config_.codec == adsp::Codec::Hevc) ? MFVideoFormat_HEVC : MFVideoFormat_H264;

        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
            nullptr,
            &outInfo,
            &activates,
            &count);
        if (FAILED(hr) || count == 0) {
            errorOut = "No hardware MF video encoder found for requested codec";
            shutdown();
            return false;
        }

        // Extract friendly name and instantiate first hardware MFT
        WCHAR* friendly = nullptr;
        UINT32 flen = 0;
        if (SUCCEEDED(activates[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly, &flen)) && friendly) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, friendly, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                friendlyName_.resize(bytes - 1);
                WideCharToMultiByte(CP_UTF8, 0, friendly, -1, friendlyName_.data(), bytes, nullptr, nullptr);
            }
            CoTaskMemFree(friendly);
        } else {
            friendlyName_ = "MF Hardware MFT";
        }

        hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft_));
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);

        if (FAILED(hr) || !mft_) {
            errorOut = "ActivateObject MFT failed";
            shutdown();
            return false;
        }

        ComPtr<IMFAttributes> mftAttrs;
        if (SUCCEEDED(mft_->GetAttributes(&mftAttrs))) {
            mftAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            mftAttrs->SetUINT32(MF_LOW_LATENCY, TRUE);
        }
        mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(dxgiMgr_.Get()));

        // Configure CodecAPI before media types
        if (SUCCEEDED(mft_.As(&codecApi_))) {
            VARIANT v{};
            VariantInit(&v);
            v.vt = VT_UI4;
            v.ulVal = config_.useVbr ? eAVEncCommonRateControlMode_PeakConstrainedVBR : eAVEncCommonRateControlMode_CBR;
            codecApi_->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);
            v.ulVal = config_.bitrateKbps * 1000u;
            codecApi_->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v);
            v.ulVal = std::max(1u, config_.fps * std::max(1u, config_.idrIntervalSec));
            codecApi_->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);
            v.vt = VT_BOOL;
            v.boolVal = VARIANT_TRUE;
            codecApi_->SetValue(&CODECAPI_AVLowLatencyMode, &v);
            v.vt = VT_UI4;
            v.ulVal = 0;
            codecApi_->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &v);
        }

        ComPtr<IMFMediaType> outType;
        MFCreateMediaType(&outType);
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outType->SetGUID(MF_MT_SUBTYPE, outInfo.guidSubtype);
        outType->SetUINT32(MF_MT_AVG_BITRATE, config_.bitrateKbps * 1000u);
        outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height);
        MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
        if (config_.codec == adsp::Codec::H264) {
            outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
        }
        hr = mft_->SetOutputType(0, outType.Get(), 0);
        if (FAILED(hr)) {
            errorOut = "MFT SetOutputType failed";
            shutdown();
            return false;
        }

        ComPtr<IMFMediaType> inType;
        MFCreateMediaType(&inType);
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height);
        MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
        hr = mft_->SetInputType(0, inType.Get(), 0);
        if (FAILED(hr)) {
            errorOut = "MFT SetInputType(NV12) failed";
            shutdown();
            return false;
        }

        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        return true;
    }

    bool encodeFrame(
        ID3D11Texture2D* bgraVramTex,
        uint64_t ptsUs,
        bool forceIdr,
        EncodedPacket& outPacket,
        std::string& errorOut) override {
        if (!mft_ || !bgraVramTex) {
            errorOut = "MF MFT not initialized";
            return false;
        }
        const uint64_t t0 = capture::nowMicrosecondsQpc();

        // GPU BGRA -> NV12 color convert
        if (cachedInputTex_ != bgraVramTex) {
            bgraInView_.Reset();
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
            ivd.FourCC = 0;
            ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            if (FAILED(videoDev_->CreateVideoProcessorInputView(bgraVramTex, vpEnum_.Get(), &ivd, &bgraInView_))) {
                errorOut = "CreateVideoProcessorInputView failed";
                return false;
            }
            cachedInputTex_ = bgraVramTex;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = bgraInView_.Get();
        videoCtx_->VideoProcessorBlt(vp_.Get(), nv12OutView_.Get(), 0, 1, &stream);
        context_->Flush();

        if ((forceIdr || frameCounter_ == 0) && codecApi_) {
            VARIANT v{};
            v.vt = VT_UI4;
            v.ulVal = 1;
            codecApi_->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &v);
        }

        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), nv12Tex_.Get(), 0, FALSE, &buf))) {
            errorOut = "MFCreateDXGISurfaceBuffer failed";
            return false;
        }
        ComPtr<IMFSample> inSample;
        MFCreateSample(&inSample);
        inSample->AddBuffer(buf.Get());
        inSample->SetSampleTime(static_cast<LONGLONG>(ptsUs * 10ULL)); // 100ns units
        inSample->SetSampleDuration(static_cast<LONGLONG>(10000000ULL / std::max(1u, config_.fps)));

        HRESULT hr = mft_->ProcessInput(0, inSample.Get(), 0);
        if (FAILED(hr)) {
            errorOut = "MFT ProcessInput failed";
            return false;
        }

        MFT_OUTPUT_STREAM_INFO osi{};
        mft_->GetOutputStreamInfo(0, &osi);
        const bool mftAllocates = (osi.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0 ||
                                  (osi.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES) != 0;

        MFT_OUTPUT_DATA_BUFFER odb{};
        ComPtr<IMFSample> outSample;
        if (!mftAllocates) {
            MFCreateSample(&outSample);
            ComPtr<IMFMediaBuffer> outBuf;
            MFCreateMemoryBuffer(std::max<DWORD>(osi.cbSize, 1024 * 1024), &outBuf);
            outSample->AddBuffer(outBuf.Get());
            odb.pSample = outSample.Get();
        }

        DWORD status = 0;
        hr = mft_->ProcessOutput(0, 1, &odb, &status);
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            ComPtr<IMFMediaType> avail;
            if (SUCCEEDED(mft_->GetOutputAvailableType(0, 0, &avail))) {
                mft_->SetOutputType(0, avail.Get(), 0);
            }
            return false;
        }
        if (SUCCEEDED(hr) && odb.pSample) {
            ComPtr<IMFMediaBuffer> outBuf;
            if (SUCCEEDED(odb.pSample->ConvertToContiguousBuffer(&outBuf))) {
                BYTE* ptr = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(outBuf->Lock(&ptr, nullptr, &len)) && ptr && len > 0) {
                    outPacket.annexB.assign(ptr, ptr + len);
                    outPacket.isKeyFrame = annexBContainsIdr(ptr, len, config_.codec);
                    outPacket.containsCsd = outPacket.isKeyFrame;
                    outBuf->Unlock();
                }
            }
            if (mftAllocates) {
                odb.pSample->Release();
            }
        }
        if (odb.pEvents) {
            odb.pEvents->Release();
        }
        outPacket.encodeTimeUs = static_cast<uint32_t>(capture::nowMicrosecondsQpc() - t0);
        ++frameCounter_;
        return !outPacket.annexB.empty();
    }

    bool updateBitrate(uint32_t newBitrateKbps) override {
        if (!codecApi_) return false;
        config_.bitrateKbps = std::clamp(newBitrateKbps, 2000u, 100000u);
        VARIANT v{};
        v.vt = VT_UI4;
        v.ulVal = config_.bitrateKbps * 1000u;
        return SUCCEEDED(codecApi_->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v));
    }

    std::string backendName() const override { return friendlyName_; }
    adsp::Codec activeCodec() const override { return config_.codec; }

    void shutdown() override {
        if (mft_) {
            mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            mft_.Reset();
        }
        codecApi_.Reset();
        bgraInView_.Reset();
        cachedInputTex_ = nullptr;
        nv12OutView_.Reset();
        nv12Tex_.Reset();
        vp_.Reset();
        vpEnum_.Reset();
        videoCtx_.Reset();
        videoDev_.Reset();
        dxgiMgr_.Reset();
        if (mfStarted_) {
            MFShutdown();
            mfStarted_ = false;
        }
    }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IMFDXGIDeviceManager> dxgiMgr_;
    ComPtr<ID3D11VideoDevice> videoDev_;
    ComPtr<ID3D11VideoContext> videoCtx_;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum_;
    ComPtr<ID3D11VideoProcessor> vp_;
    ComPtr<ID3D11Texture2D> nv12Tex_;
    ComPtr<ID3D11VideoProcessorOutputView> nv12OutView_;
    ComPtr<ID3D11VideoProcessorInputView> bgraInView_;
    ID3D11Texture2D* cachedInputTex_{nullptr};

    ComPtr<IMFTransform> mft_;
    ComPtr<ICodecAPI> codecApi_;
    EncoderConfig config_{};
    std::string friendlyName_{"MF Hardware MFT"};
    bool mfStarted_{false};
    uint64_t frameCounter_{0};
};

} // namespace

std::unique_ptr<IEncoderBackend> createBestHardwareEncoder(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const EncoderConfig& config,
    std::string& diagnosticLog) {
    std::ostringstream log;
    std::string err;

    auto nvenc = std::make_unique<NvencDirectEncoder>();
    if (nvenc->initialize(device, context, config, err)) {
        diagnosticLog = "Selected " + nvenc->backendName();
        return nvenc;
    }
    log << "NVENC Direct skipped: " << err << "; ";

    auto mf = std::make_unique<MfHardwareEncoder>();
    if (mf->initialize(device, context, config, err)) {
        diagnosticLog = log.str() + "Selected " + mf->backendName();
        return mf;
    }
    log << "MF HW MFT failed: " << err;
    diagnosticLog = log.str();
    return nullptr;
}

} // namespace aegis::encode
