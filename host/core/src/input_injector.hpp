#pragma once

#include "adsp_protocol.hpp"
#include "input_math.hpp"
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace aegis::input {

class InputInjector {
public:
    InputInjector() = default;
    ~InputInjector();

    bool initialize();
    void shutdown();

    void setTargetMonitorRect(const RECT& r);

    void injectTouchBatch(const std::vector<adsp::TouchContact>& contacts);
    void injectPen(const adsp::InputPenPayload& pen);
    void injectMouse(const adsp::InputMousePayload& mouse);
    void injectKey(const adsp::InputKeyPayload& key);

private:
    static DisplayRect getVirtualDesktopBounds();

    std::mutex mutex_;
    HSYNTHETICPOINTERDEVICE touchDev_{nullptr};
    HSYNTHETICPOINTERDEVICE penDev_{nullptr};
    DisplayRect targetRect_{0, 0, 1920, 1080};
    std::unordered_map<uint32_t, PixelPoint> activeTouchPoints_;
};

} // namespace aegis::input
