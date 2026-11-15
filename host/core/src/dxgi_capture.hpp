#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace aegis::capture {

struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> gpuBgraTexture;
    uint64_t captureTimestampUs{0};
    uint32_t width{0};
    uint32_t height{0};
    bool dirty{false};
};

class DxgiDuplicator {
public:
    DxgiDuplicator() = default;
    ~DxgiDuplicator();

    bool initialize(const LUID& adapterLuid, const std::wstring& targetGdiName, std::string& errorOut);
    void shutdown();

    void setMonitorRect(const RECT& rc) { monitorRect_ = rc; }
    void setCursorEnabled(bool enabled) { cursorEnabled_.store(enabled); }
    bool cursorEnabled() const { return cursorEnabled_.load(); }

    // Acquires next frame into an owned VRAM texture so DWM is immediately released.
    // Composites desktop cursor (DXGI pointer shape + GDI GetCursorInfo fallback) when enabled.
    // Returns true if desktop OR cursor changed; returns false on idle timeout (check accessLost).
    bool acquireFrame(uint32_t timeoutMs, CapturedFrame& outFrame, bool& accessLost, std::string& errorOut);

    bool reacquire(std::string& errorOut);

    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    std::string adapterDescription() const { return adapterDesc_; }

private:
    bool bindOutputDuplication(std::string& errorOut);
    bool initCursorShaderResources();
    void updateCursorFromDxgi(const DXGI_OUTDUPL_FRAME_INFO& frameInfo);
    bool updateCursorFromGdiFallback();
    void uploadCursorBitmap(uint32_t w, uint32_t h, const std::vector<uint8_t>& andBgra, const std::vector<uint8_t>& xorBgra);
    void compositeCursorToOutput();

    LUID adapterLuid_{};
    std::wstring targetGdiName_;
    std::string adapterDesc_;
    RECT monitorRect_{0, 0, 1920, 1080};
    uint32_t width_{0};
    uint32_t height_{0};
    std::atomic<bool> cursorEnabled_{true};

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopVramTex_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> compositedVramTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> desktopSrv_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> compositedRtv_;

    // GPU cursor overlay pipeline
    Microsoft::WRL::ComPtr<ID3D11VertexShader> cursorVs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> cursorPs_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cursorCb_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> pointSampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> cursorAndTex_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> cursorXorTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cursorAndSrv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cursorXorSrv_;
    bool cursorShaderReady_{false};

    // Cached cursor state
    std::vector<uint8_t> dxgiShapeBuf_;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO dxgiShapeInfo_{};
    bool hasDxgiShape_{false};
    bool dxgiPointerVisible_{false};
    POINT dxgiPointerPos_{0, 0};
    bool hasDesktopImage_{false};

    HCURSOR lastGdiCursor_{nullptr};
    bool cursorVisibleNow_{false};
    int cursorDrawX_{0};
    int cursorDrawY_{0};
    uint32_t cursorW_{0};
    uint32_t cursorH_{0};
    uint32_t curHotspotX_{0};
    uint32_t curHotspotY_{0};
    int prevEmittedCurX_{INT_MIN};
    int prevEmittedCurY_{INT_MIN};
    bool prevEmittedVisible_{false};
};

uint64_t nowMicrosecondsQpc();

} // namespace aegis::capture
