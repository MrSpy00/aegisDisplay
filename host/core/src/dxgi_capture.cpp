#include "dxgi_capture.hpp"

#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cstring>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace aegis::capture {

namespace {

constexpr const char* kCursorOverlayHlsl = R"(
cbuffer CursorCB : register(b0) {
    float4 dstNorm; // x, y, w, h in [0..1]
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};
VSOut VSMain(uint id : SV_VertexID) {
    float2 c = float2(id & 1, (id >> 1) & 1);
    float2 sc = dstNorm.xy + c * dstNorm.zw;
    VSOut o;
    o.pos = float4(sc.x * 2.0 - 1.0, 1.0 - sc.y * 2.0, 0.0, 1.0);
    o.uv = c;
    return o;
}

Texture2D<float4> desktopTex : register(t0);
Texture2D<float4> curAndTex  : register(t1);
Texture2D<float4> curXorTex  : register(t2);
SamplerState sampPoint : register(s0);

float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 bg = desktopTex.Load(int3(int2(pos.xy), 0));
    float4 andS = curAndTex.Sample(sampPoint, uv);
    float4 xorS = curXorTex.Sample(sampPoint, uv);
    if (xorS.a > 0.5) {
        uint3 bgU  = uint3(round(bg.rgb * 255.0));
        uint3 andU = uint3(round(andS.rgb * 255.0));
        uint3 xorU = uint3(round(xorS.rgb * 255.0));
        uint3 resU = (bgU & andU) ^ xorU;
        return float4(float3(resU) / 255.0, 1.0);
    } else {
        float a = andS.a;
        return float4(saturate(xorS.rgb + bg.rgb * (1.0 - a)), 1.0);
    }
}
)";

using FnD3DCompile = HRESULT(WINAPI*)(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
    LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

std::string wideToUtf8(const wchar_t* ws) {
    if (!ws || !ws[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

uint64_t nowMicrosecondsQpc() {
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>((now.QuadPart * 1000000LL) / freq.QuadPart);
}

DxgiDuplicator::~DxgiDuplicator() {
    shutdown();
}

bool DxgiDuplicator::initialize(const LUID& adapterLuid, const std::wstring& targetGdiName, std::string& errorOut) {
    shutdown();
    adapterLuid_ = adapterLuid;
    targetGdiName_ = targetGdiName;

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        errorOut = "CreateDXGIFactory1 failed";
        return false;
    }

    ComPtr<IDXGIAdapter1> ownerAdapter;
    ComPtr<IDXGIOutput> matchedOutput;

    for (UINT ai = 0;; ++ai) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        for (UINT oi = 0;; ++oi) {
            ComPtr<IDXGIOutput> out;
            if (adapter->EnumOutputs(oi, &out) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(out->GetDesc(&od))) {
                if (!targetGdiName_.empty() && targetGdiName_ == od.DeviceName) {
                    ownerAdapter = adapter;
                    matchedOutput = out;
                    break;
                }
            }
        }
        if (matchedOutput) break;
    }

    if (!matchedOutput) {
        // Fallback: match adapterLuid output 0
        for (UINT ai = 0;; ++ai) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 ad{};
            if (SUCCEEDED(adapter->GetDesc1(&ad)) &&
                ad.AdapterLuid.LowPart == adapterLuid_.LowPart &&
                ad.AdapterLuid.HighPart == adapterLuid_.HighPart) {
                ComPtr<IDXGIOutput> out0;
                if (SUCCEEDED(adapter->EnumOutputs(0, &out0))) {
                    ownerAdapter = adapter;
                    matchedOutput = out0;
                    break;
                }
            }
        }
    }

    if (!matchedOutput || !ownerAdapter) {
        errorOut = "Unable to find DXGI output for target display";
        return false;
    }

    DXGI_ADAPTER_DESC1 ad{};
    if (SUCCEEDED(ownerAdapter->GetDesc1(&ad))) {
        adapterDesc_ = wideToUtf8(ad.Description);
    }

    DXGI_OUTPUT_DESC od{};
    matchedOutput->GetDesc(&od);
    width_ = static_cast<uint32_t>(std::max<LONG>(1, od.DesktopCoordinates.right - od.DesktopCoordinates.left));
    height_ = static_cast<uint32_t>(std::max<LONG>(1, od.DesktopCoordinates.bottom - od.DesktopCoordinates.top));
    monitorRect_ = od.DesktopCoordinates;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL achieved{};
    hr = D3D11CreateDevice(
        ownerAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,
        2,
        D3D11_SDK_VERSION,
        &device_,
        &achieved,
        &context_);
    if (FAILED(hr) || !device_ || !context_) {
        errorOut = "D3D11CreateDevice failed for capture output";
        return false;
    }

    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(device_.As(&mt)) && mt) {
        mt->SetMultithreadProtected(TRUE);
    }

    if (FAILED(matchedOutput.As(&output1_)) || !output1_) {
        errorOut = "IDXGIOutput1 query failed";
        return false;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    hr = device_->CreateTexture2D(&td, nullptr, &desktopVramTex_);
    if (FAILED(hr) || !desktopVramTex_) {
        errorOut = "Failed to allocate desktop VRAM texture";
        return false;
    }
    hr = device_->CreateTexture2D(&td, nullptr, &compositedVramTex_);
    if (FAILED(hr) || !compositedVramTex_) {
        errorOut = "Failed to allocate composited VRAM texture";
        return false;
    }

    device_->CreateShaderResourceView(desktopVramTex_.Get(), nullptr, &desktopSrv_);
    device_->CreateRenderTargetView(compositedVramTex_.Get(), nullptr, &compositedRtv_);
    initCursorShaderResources();

    return bindOutputDuplication(errorOut);
}

bool DxgiDuplicator::initCursorShaderResources() {
    HMODULE mod = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!mod) return false;
    auto fnCompile = reinterpret_cast<FnD3DCompile>(GetProcAddress(mod, "D3DCompile"));
    if (!fnCompile) return false;

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    if (FAILED(fnCompile(
            kCursorOverlayHlsl, std::strlen(kCursorOverlayHlsl), "cursor_overlay.hlsl",
            nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob))) {
        return false;
    }
    if (FAILED(fnCompile(
            kCursorOverlayHlsl, std::strlen(kCursorOverlayHlsl), "cursor_overlay.hlsl",
            nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob))) {
        return false;
    }

    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &cursorVs_)) ||
        FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &cursorPs_))) {
        return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 16;
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device_->CreateBuffer(&cbd, nullptr, &cursorCb_))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device_->CreateSamplerState(&sd, &pointSampler_))) {
        return false;
    }

    cursorShaderReady_ = (desktopSrv_ && compositedRtv_ && cursorVs_ && cursorPs_ && cursorCb_ && pointSampler_);
    return cursorShaderReady_;
}

bool DxgiDuplicator::bindOutputDuplication(std::string& errorOut) {
    duplication_.Reset();
    if (!output1_ || !device_) {
        errorOut = "Duplicator not initialized";
        return false;
    }

    const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    ComPtr<IDXGIOutput5> output5;
    output1_.As(&output5);
    HRESULT hr = E_FAIL;

    for (int attempt = 0; attempt < 15; ++attempt) {
        if (HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_READ)) {
            SetThreadDesktop(desk);
            CloseDesktop(desk);
        }

        if (output5) {
            hr = output5->DuplicateOutput1(device_.Get(), 0, 1, formats, &duplication_);
            if (SUCCEEDED(hr) && duplication_) break;
        }
        hr = output1_->DuplicateOutput(device_.Get(), &duplication_);
        if (SUCCEEDED(hr) && duplication_) break;

        if (hr != E_ACCESSDENIED &&
            hr != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE &&
            hr != DXGI_ERROR_UNSUPPORTED &&
            hr != DXGI_ERROR_SESSION_DISCONNECTED) {
            break;
        }
        Sleep(100);
    }

    if (FAILED(hr) || !duplication_) {
        std::ostringstream oss;
        oss << "DuplicateOutput failed hr=0x" << std::hex << static_cast<uint32_t>(hr)
            << " adapter=" << adapterDesc_;
        errorOut = oss.str();
        return false;
    }
    return true;
}

bool DxgiDuplicator::reacquire(std::string& errorOut) {
    return bindOutputDuplication(errorOut);
}

void DxgiDuplicator::uploadCursorBitmap(
    uint32_t w,
    uint32_t h,
    const std::vector<uint8_t>& andBgra,
    const std::vector<uint8_t>& xorBgra) {
    if (!device_ || w == 0 || h == 0) return;

    if (w != cursorW_ || h != cursorH_ || !cursorAndTex_ || !cursorXorTex_) {
        cursorAndTex_.Reset();
        cursorXorTex_.Reset();
        cursorAndSrv_.Reset();
        cursorXorSrv_.Reset();

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&td, nullptr, &cursorAndTex_)) ||
            FAILED(device_->CreateTexture2D(&td, nullptr, &cursorXorTex_))) {
            return;
        }
        device_->CreateShaderResourceView(cursorAndTex_.Get(), nullptr, &cursorAndSrv_);
        device_->CreateShaderResourceView(cursorXorTex_.Get(), nullptr, &cursorXorSrv_);
        cursorW_ = w;
        cursorH_ = h;
    }

    context_->UpdateSubresource(cursorAndTex_.Get(), 0, nullptr, andBgra.data(), w * 4u, 0);
    context_->UpdateSubresource(cursorXorTex_.Get(), 0, nullptr, xorBgra.data(), w * 4u, 0);
}

void DxgiDuplicator::updateCursorFromDxgi(const DXGI_OUTDUPL_FRAME_INFO& frameInfo) {
    dxgiPointerVisible_ = (frameInfo.PointerPosition.Visible != FALSE);
    dxgiPointerPos_ = frameInfo.PointerPosition.Position;

    if (frameInfo.PointerShapeBufferSize > 0 && duplication_) {
        dxgiShapeBuf_.resize(frameInfo.PointerShapeBufferSize);
        UINT required = 0;
        HRESULT hr = duplication_->GetFramePointerShape(
            static_cast<UINT>(dxgiShapeBuf_.size()),
            dxgiShapeBuf_.data(),
            &required,
            &dxgiShapeInfo_);
        if (SUCCEEDED(hr) && dxgiShapeInfo_.Width > 0 && dxgiShapeInfo_.Height > 0) {
            const uint32_t w = dxgiShapeInfo_.Width;
            const uint32_t h = (dxgiShapeInfo_.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME)
                ? (dxgiShapeInfo_.Height / 2u)
                : dxgiShapeInfo_.Height;
            if (h > 0) {
                std::vector<uint8_t> andImg(static_cast<size_t>(w) * h * 4u, 0);
                std::vector<uint8_t> xorImg(static_cast<size_t>(w) * h * 4u, 0);

                if (dxgiShapeInfo_.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
                    for (uint32_t y = 0; y < h; ++y) {
                        const uint8_t* row = dxgiShapeBuf_.data() + static_cast<size_t>(y) * dxgiShapeInfo_.Pitch;
                        for (uint32_t x = 0; x < w; ++x) {
                            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
                            uint8_t b = row[x * 4u + 0];
                            uint8_t g = row[x * 4u + 1];
                            uint8_t r = row[x * 4u + 2];
                            uint8_t a = row[x * 4u + 3];
                            // Convert straight alpha to premultiplied if needed
                            if (std::max({b, g, r}) > a && a > 0) {
                                b = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127u) / 255u);
                                g = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127u) / 255u);
                                r = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127u) / 255u);
                            }
                            andImg[idx + 3] = a; // alpha stored in A channel
                            xorImg[idx + 0] = b;
                            xorImg[idx + 1] = g;
                            xorImg[idx + 2] = r;
                            xorImg[idx + 3] = 0; // mode=alpha blend
                        }
                    }
                } else if (dxgiShapeInfo_.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
                    for (uint32_t y = 0; y < h; ++y) {
                        const uint8_t* row = dxgiShapeBuf_.data() + static_cast<size_t>(y) * dxgiShapeInfo_.Pitch;
                        for (uint32_t x = 0; x < w; ++x) {
                            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
                            const uint8_t b = row[x * 4u + 0];
                            const uint8_t g = row[x * 4u + 1];
                            const uint8_t r = row[x * 4u + 2];
                            const uint8_t a = row[x * 4u + 3];
                            const uint8_t maskByte = (a != 0) ? 0xFF : 0x00;
                            andImg[idx + 0] = maskByte;
                            andImg[idx + 1] = maskByte;
                            andImg[idx + 2] = maskByte;
                            andImg[idx + 3] = 0xFF;
                            xorImg[idx + 0] = b;
                            xorImg[idx + 1] = g;
                            xorImg[idx + 2] = r;
                            xorImg[idx + 3] = 0xFF; // mode=bitwise AND/XOR
                        }
                    }
                } else { // MONOCHROME: top half AND mask, bottom half XOR mask (1bpp MSB first)
                    for (uint32_t y = 0; y < h; ++y) {
                        const uint8_t* andRow = dxgiShapeBuf_.data() + static_cast<size_t>(y) * dxgiShapeInfo_.Pitch;
                        const uint8_t* xorRow = dxgiShapeBuf_.data() + static_cast<size_t>(y + h) * dxgiShapeInfo_.Pitch;
                        for (uint32_t x = 0; x < w; ++x) {
                            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
                            const uint8_t bitMask = static_cast<uint8_t>(0x80u >> (x & 7u));
                            const uint8_t andVal = (andRow[x >> 3u] & bitMask) ? 0xFF : 0x00;
                            const uint8_t xorVal = (xorRow[x >> 3u] & bitMask) ? 0xFF : 0x00;
                            andImg[idx + 0] = andVal;
                            andImg[idx + 1] = andVal;
                            andImg[idx + 2] = andVal;
                            andImg[idx + 3] = 0xFF;
                            xorImg[idx + 0] = xorVal;
                            xorImg[idx + 1] = xorVal;
                            xorImg[idx + 2] = xorVal;
                            xorImg[idx + 3] = 0xFF; // mode=bitwise AND/XOR
                        }
                    }
                }

                uploadCursorBitmap(w, h, andImg, xorImg);
                curHotspotX_ = dxgiShapeInfo_.HotSpot.x;
                curHotspotY_ = dxgiShapeInfo_.HotSpot.y;
                hasDxgiShape_ = true;
            }
        }
    }
}

bool DxgiDuplicator::updateCursorFromGdiFallback() {
    CURSORINFO ci{};
    ci.cbSize = sizeof(CURSORINFO);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) {
        bool changed = cursorVisibleNow_;
        cursorVisibleNow_ = false;
        return changed;
    }

    const int relX = ci.ptScreenPos.x - monitorRect_.left;
    const int relY = ci.ptScreenPos.y - monitorRect_.top;
    const bool insideMonitor =
        relX >= -64 && relX < static_cast<int>(width_) + 64 &&
        relY >= -64 && relY < static_cast<int>(height_) + 64 &&
        ci.ptScreenPos.x >= monitorRect_.left && ci.ptScreenPos.x < monitorRect_.right &&
        ci.ptScreenPos.y >= monitorRect_.top && ci.ptScreenPos.y < monitorRect_.bottom;

    if (!insideMonitor) {
        bool changed = cursorVisibleNow_;
        cursorVisibleNow_ = false;
        return changed;
    }

    if (ci.hCursor != lastGdiCursor_ || !hasDxgiShape_) {
        ICONINFO ii{};
        if (GetIconInfo(ci.hCursor, &ii)) {
            BITMAP bmMask{};
            GetObjectW(ii.hbmMask, sizeof(BITMAP), &bmMask);
            uint32_t cw = static_cast<uint32_t>(std::clamp<LONG>(bmMask.bmWidth, 16, 128));
            uint32_t ch = static_cast<uint32_t>(std::clamp<LONG>(
                ii.hbmColor ? bmMask.bmHeight : (bmMask.bmHeight / 2), 16, 128));

            HDC screenDc = GetDC(nullptr);
            HDC memDc = CreateCompatibleDC(screenDc);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = static_cast<LONG>(cw);
            bi.bmiHeader.biHeight = -static_cast<LONG>(ch);
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            void* bitsBlack = nullptr;
            void* bitsWhite = nullptr;
            HBITMAP dibBlack = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bitsBlack, nullptr, 0);
            HBITMAP dibWhite = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bitsWhite, nullptr, 0);

            if (dibBlack && dibWhite && bitsBlack && bitsWhite) {
                std::memset(bitsBlack, 0x00, static_cast<size_t>(cw) * ch * 4u);
                std::memset(bitsWhite, 0xFF, static_cast<size_t>(cw) * ch * 4u);

                HGDIOBJ old = SelectObject(memDc, dibBlack);
                DrawIconEx(memDc, 0, 0, ci.hCursor, static_cast<int>(cw), static_cast<int>(ch), 0, nullptr, DI_NORMAL);
                SelectObject(memDc, dibWhite);
                DrawIconEx(memDc, 0, 0, ci.hCursor, static_cast<int>(cw), static_cast<int>(ch), 0, nullptr, DI_NORMAL);
                SelectObject(memDc, old);
                GdiFlush();

                const auto* onB = static_cast<const uint8_t*>(bitsBlack);
                const auto* onW = static_cast<const uint8_t*>(bitsWhite);
                const size_t pxCount = static_cast<size_t>(cw) * ch;
                std::vector<uint8_t> andImg(pxCount * 4u, 0);
                std::vector<uint8_t> xorImg(pxCount * 4u, 0);

                // Dual-background reconstruction handles BOTH alpha color cursors and XOR monochrome I-beams!
                for (size_t i = 0; i < pxCount; ++i) {
                    const size_t o = i * 4u;
                    const uint8_t b0 = onB[o + 0], g0 = onB[o + 1], r0 = onB[o + 2];
                    const uint8_t b1 = onW[o + 0], g1 = onW[o + 1], r1 = onW[o + 2];

                    if (b0 == 0 && b1 == 255 && g0 == 0 && g1 == 255 && r0 == 0 && r1 == 255) {
                        // Transparent pixel
                        andImg[o + 0] = 0xFF; andImg[o + 1] = 0xFF; andImg[o + 2] = 0xFF; andImg[o + 3] = 0xFF;
                        xorImg[o + 0] = 0x00; xorImg[o + 1] = 0x00; xorImg[o + 2] = 0x00; xorImg[o + 3] = 0xFF;
                    } else if (b0 == 255 && b1 == 0 && g0 == 255 && g1 == 0 && r0 == 255 && r1 == 0) {
                        // Pure XOR inversion pixel (I-beam!)
                        andImg[o + 0] = 0xFF; andImg[o + 1] = 0xFF; andImg[o + 2] = 0xFF; andImg[o + 3] = 0xFF;
                        xorImg[o + 0] = 0xFF; xorImg[o + 1] = 0xFF; xorImg[o + 2] = 0xFF; xorImg[o + 3] = 0xFF;
                    } else {
                        // Recover alpha and premultiplied RGB:
                        // onBlack = srcPremul, onWhite = srcPremul + (1 - alpha)
                        const int oneMinusA = std::clamp<int>(
                            ((static_cast<int>(b1) - b0) + (static_cast<int>(g1) - g0) + (static_cast<int>(r1) - r0)) / 3,
                            0, 255);
                        const uint8_t alpha = static_cast<uint8_t>(255 - oneMinusA);
                        andImg[o + 3] = alpha;
                        xorImg[o + 0] = b0;
                        xorImg[o + 1] = g0;
                        xorImg[o + 2] = r0;
                        xorImg[o + 3] = 0; // mode=alpha blend
                    }
                }

                uploadCursorBitmap(cw, ch, andImg, xorImg);
                curHotspotX_ = ii.xHotspot;
                curHotspotY_ = ii.yHotspot;
                lastGdiCursor_ = ci.hCursor;
            }

            if (dibBlack) DeleteObject(dibBlack);
            if (dibWhite) DeleteObject(dibWhite);
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
        }
    }

    cursorVisibleNow_ = (cursorW_ > 0 && cursorH_ > 0);
    cursorDrawX_ = relX - static_cast<int>(curHotspotX_);
    cursorDrawY_ = relY - static_cast<int>(curHotspotY_);

    return (cursorVisibleNow_ != prevEmittedVisible_) ||
           (cursorDrawX_ != prevEmittedCurX_) ||
           (cursorDrawY_ != prevEmittedCurY_);
}

void DxgiDuplicator::compositeCursorToOutput() {
    if (!compositedVramTex_ || !desktopVramTex_) return;

    context_->CopyResource(compositedVramTex_.Get(), desktopVramTex_.Get());

    if (!cursorEnabled_.load() || !cursorVisibleNow_ || !cursorShaderReady_ ||
        !cursorAndSrv_ || !cursorXorSrv_ || cursorW_ == 0 || cursorH_ == 0 ||
        width_ == 0 || height_ == 0) {
        return;
    }

    const float dstRect[4] = {
        static_cast<float>(cursorDrawX_) / static_cast<float>(width_),
        static_cast<float>(cursorDrawY_) / static_cast<float>(height_),
        static_cast<float>(cursorW_) / static_cast<float>(width_),
        static_cast<float>(cursorH_) / static_cast<float>(height_),
    };
    context_->UpdateSubresource(cursorCb_.Get(), 0, nullptr, dstRect, 0, 0);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = {compositedRtv_.Get()};
    context_->OMSetRenderTargets(1, rtvs, nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(cursorVs_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, cursorCb_.GetAddressOf());
    context_->PSSetShader(cursorPs_.Get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[] = {desktopSrv_.Get(), cursorAndSrv_.Get(), cursorXorSrv_.Get()};
    context_->PSSetShaderResources(0, 3, srvs);
    context_->PSSetSamplers(0, 1, pointSampler_.GetAddressOf());

    context_->Draw(4, 0);

    // Unbind SRVs/RTV so desktopVramTex_ & compositedVramTex_ don't collide on next copy/encode
    ID3D11ShaderResourceView* nullSrvs[3] = {nullptr, nullptr, nullptr};
    ID3D11RenderTargetView* nullRtv[1] = {nullptr};
    context_->PSSetShaderResources(0, 3, nullSrvs);
    context_->OMSetRenderTargets(1, nullRtv, nullptr);
}

bool DxgiDuplicator::acquireFrame(
    uint32_t timeoutMs,
    CapturedFrame& outFrame,
    bool& accessLost,
    std::string& errorOut) {
    accessLost = false;
    if (!duplication_) {
        accessLost = true;
        errorOut = "No active duplication handle";
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopRes;
    HRESULT hr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, &desktopRes);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (hasDesktopImage_ && cursorEnabled_.load() && updateCursorFromGdiFallback()) {
            compositeCursorToOutput();
            prevEmittedVisible_ = cursorVisibleNow_;
            prevEmittedCurX_ = cursorDrawX_;
            prevEmittedCurY_ = cursorDrawY_;
            outFrame.gpuBgraTexture = compositedVramTex_;
            outFrame.captureTimestampUs = nowMicrosecondsQpc();
            outFrame.width = width_;
            outFrame.height = height_;
            outFrame.dirty = true;
            return true;
        }
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_INVALID_CALL) {
        accessLost = true;
        duplication_.Reset();
        std::ostringstream oss;
        oss << "Duplication access lost hr=0x" << std::hex << static_cast<uint32_t>(hr);
        errorOut = oss.str();
        return false;
    }
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "AcquireNextFrame failed hr=0x" << std::hex << static_cast<uint32_t>(hr);
        errorOut = oss.str();
        return false;
    }

    const uint64_t ts = nowMicrosecondsQpc();
    const bool hasImage = (frameInfo.LastPresentTime.QuadPart != 0) || (frameInfo.AccumulatedFrames > 0);
    const bool hasMouse = (frameInfo.LastMouseUpdateTime.QuadPart != 0);

    if (hasImage && desktopRes) {
        ComPtr<ID3D11Texture2D> srcTex;
        if (SUCCEEDED(desktopRes.As(&srcTex))) {
            context_->CopyResource(desktopVramTex_.Get(), srcTex.Get());
            hasDesktopImage_ = true;
        }
    }

    if (hasMouse && cursorEnabled_.load()) {
        updateCursorFromDxgi(frameInfo);
    }

    duplication_->ReleaseFrame();

    if (cursorEnabled_.load()) {
        // Prefer DXGI pointer position when DXGI delivered a shape & visibility, otherwise GDI fallback
        if (hasDxgiShape_ && hasMouse) {
            cursorVisibleNow_ = dxgiPointerVisible_ && (cursorW_ > 0 && cursorH_ > 0);
            cursorDrawX_ = dxgiPointerPos_.x;
            cursorDrawY_ = dxgiPointerPos_.y;
        } else {
            updateCursorFromGdiFallback();
        }
    } else {
        cursorVisibleNow_ = false;
    }

    if (!hasImage && !hasMouse) {
        return false;
    }

    if (cursorEnabled_.load() && cursorVisibleNow_ && cursorShaderReady_) {
        compositeCursorToOutput();
        outFrame.gpuBgraTexture = compositedVramTex_;
    } else {
        outFrame.gpuBgraTexture = desktopVramTex_;
    }

    prevEmittedVisible_ = cursorVisibleNow_;
    prevEmittedCurX_ = cursorDrawX_;
    prevEmittedCurY_ = cursorDrawY_;

    outFrame.captureTimestampUs = ts;
    outFrame.width = width_;
    outFrame.height = height_;
    outFrame.dirty = true;
    return true;
}

void DxgiDuplicator::shutdown() {
    duplication_.Reset();
    output1_.Reset();
    cursorAndSrv_.Reset();
    cursorXorSrv_.Reset();
    cursorAndTex_.Reset();
    cursorXorTex_.Reset();
    cursorCb_.Reset();
    pointSampler_.Reset();
    cursorVs_.Reset();
    cursorPs_.Reset();
    desktopSrv_.Reset();
    compositedRtv_.Reset();
    desktopVramTex_.Reset();
    compositedVramTex_.Reset();
    context_.Reset();
    device_.Reset();
    dxgiShapeBuf_.clear();
    hasDxgiShape_ = false;
    dxgiPointerVisible_ = false;
    hasDesktopImage_ = false;
    lastGdiCursor_ = nullptr;
    cursorShaderReady_ = false;
    width_ = 0;
    height_ = 0;
}

} // namespace aegis::capture
