#include "input_injector.hpp"
#include <algorithm>
#include <cstring>

namespace aegis::input {

InputInjector::~InputInjector() {
    shutdown();
}

bool InputInjector::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!touchDev_) {
        touchDev_ = CreateSyntheticPointerDevice(PT_TOUCH, 10, POINTER_FEEDBACK_DEFAULT);
    }
    if (!penDev_) {
        penDev_ = CreateSyntheticPointerDevice(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
    }
    return touchDev_ != nullptr || penDev_ != nullptr;
}

void InputInjector::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (touchDev_) {
        DestroySyntheticPointerDevice(touchDev_);
        touchDev_ = nullptr;
    }
    if (penDev_) {
        DestroySyntheticPointerDevice(penDev_);
        penDev_ = nullptr;
    }
    activeTouchPoints_.clear();
}

void InputInjector::setTargetMonitorRect(const RECT& r) {
    std::lock_guard<std::mutex> lock(mutex_);
    targetRect_ = {r.left, r.top, r.right, r.bottom};
}

DisplayRect InputInjector::getVirtualDesktopBounds() {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return {vx, vy, vx + vw, vy + vh};
}

void InputInjector::injectTouchBatch(const std::vector<adsp::TouchContact>& contacts) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!touchDev_ || contacts.empty()) return;

    const size_t count = std::min<size_t>(contacts.size(), 10);
    std::vector<POINTER_TYPE_INFO> infos(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        PixelPoint pt = mapNormToMonitorPixel(c.xNorm, c.yNorm, targetRect_);

        POINTER_TYPE_INFO pti{};
        pti.type = PT_TOUCH;
        auto& ti = pti.touchInfo;
        ti.pointerInfo.pointerType = PT_TOUCH;
        ti.pointerInfo.pointerId = c.pointerId;
        ti.pointerInfo.ptPixelLocation.x = pt.x;
        ti.pointerInfo.ptPixelLocation.y = pt.y;
        ti.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_PRESSURE;
        const int32_t r = std::max<int32_t>(2, static_cast<int32_t>(c.contactSize));
        ti.rcContact = {pt.x - r, pt.y - r, pt.x + r, pt.y + r};
        ti.pressure = std::min<UINT32>(1024u, c.pressure);

        switch (static_cast<adsp::TouchAction>(c.action)) {
        case adsp::TouchAction::Down:
            ti.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
            activeTouchPoints_[c.pointerId] = pt;
            break;
        case adsp::TouchAction::Move:
            ti.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
            activeTouchPoints_[c.pointerId] = pt;
            break;
        case adsp::TouchAction::Up:
            ti.pointerInfo.pointerFlags = POINTER_FLAG_UP;
            activeTouchPoints_.erase(c.pointerId);
            break;
        case adsp::TouchAction::Cancel:
        default:
            ti.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CANCELED;
            activeTouchPoints_.erase(c.pointerId);
            break;
        }
        infos[i] = pti;
    }

    InjectSyntheticPointerInput(touchDev_, infos.data(), static_cast<UINT32>(infos.size()));
}

void InputInjector::injectPen(const adsp::InputPenPayload& pen) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!penDev_) return;

    const PixelPoint pt = mapNormToMonitorPixel(pen.xNorm, pen.yNorm, targetRect_);
    POINTER_TYPE_INFO pti{};
    pti.type = PT_PEN;
    auto& pi = pti.penInfo;
    pi.pointerInfo.pointerType = PT_PEN;
    pi.pointerInfo.pointerId = 0; // PT_PEN maxCount=1 -> pointerId 0
    pi.pointerInfo.ptPixelLocation.x = pt.x;
    pi.pointerInfo.ptPixelLocation.y = pt.y;

    pi.penMask = PEN_MASK_PRESSURE | PEN_MASK_ROTATION | PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
    pi.pressure = std::min<UINT32>(1024u, pen.pressure);
    pi.rotation = pen.rotation % 360u;
    pi.tiltX = std::clamp<INT32>(pen.tiltX, -90, 90);
    pi.tiltY = std::clamp<INT32>(pen.tiltY, -90, 90);

    if (pen.flags & adsp::PenFlagBarrel) pi.penFlags |= PEN_FLAG_BARREL;
    if (pen.flags & adsp::PenFlagEraser) pi.penFlags |= PEN_FLAG_ERASER;

    switch (static_cast<adsp::PenAction>(pen.action)) {
    case adsp::PenAction::Down:
        pi.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
        break;
    case adsp::PenAction::Move:
        pi.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
        break;
    case adsp::PenAction::Up:
        pi.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_INRANGE;
        break;
    case adsp::PenAction::Hover:
    default:
        pi.pointerInfo.pointerFlags = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE;
        break;
    }

    InjectSyntheticPointerInput(penDev_, &pti, 1);
}

void InputInjector::injectMouse(const adsp::InputMousePayload& mouse) {
    INPUT in{};
    in.type = INPUT_MOUSE;

    if (mouse.flags & adsp::MouseRelative) {
        const int32_t relX = static_cast<int16_t>(mouse.xNorm);
        const int32_t relY = static_cast<int16_t>(mouse.yNorm);
        in.mi.dx = relX;
        in.mi.dy = relY;
        if (relX != 0 || relY != 0) {
            in.mi.dwFlags |= MOUSEEVENTF_MOVE;
        }
    } else {
        DisplayRect target{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            target = targetRect_;
        }
        const PixelPoint px = mapNormToMonitorPixel(mouse.xNorm, mouse.yNorm, target);
        const PixelPoint vd = mapPixelToVirtualDesk(px.x, px.y, getVirtualDesktopBounds());
        in.mi.dx = vd.x;
        in.mi.dy = vd.y;
        in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    }

    if (mouse.flags & adsp::MouseLeftDown)   in.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
    if (mouse.flags & adsp::MouseLeftUp)     in.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
    if (mouse.flags & adsp::MouseRightDown)  in.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
    if (mouse.flags & adsp::MouseRightUp)    in.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
    if (mouse.flags & adsp::MouseMiddleDown) in.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
    if (mouse.flags & adsp::MouseMiddleUp)   in.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;

    if (mouse.flags & adsp::MouseWheel) {
        in.mi.dwFlags |= MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(static_cast<int32_t>(mouse.wheelDelta));
    } else if (mouse.flags & adsp::MouseHWheel) {
        in.mi.dwFlags |= MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = static_cast<DWORD>(static_cast<int32_t>(mouse.hWheelDelta));
    }

    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::injectKey(const adsp::InputKeyPayload& key) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    if (key.scanCode != 0) {
        in.ki.wScan = key.scanCode;
        in.ki.dwFlags = KEYEVENTF_SCANCODE;
    } else {
        in.ki.wVk = key.vkCode;
    }
    if (key.isExtended) {
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!key.isDown) {
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    SendInput(1, &in, sizeof(INPUT));
}

} // namespace aegis::input
