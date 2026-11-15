#include "input_math.hpp"
#include <algorithm>
#include <cmath>

namespace aegis::input {

PixelPoint mapNormToMonitorPixel(uint16_t xNorm, uint16_t yNorm, const DisplayRect& monitorRect) {
    const int64_t width = static_cast<int64_t>(monitorRect.right) - monitorRect.left;
    const int64_t height = static_cast<int64_t>(monitorRect.bottom) - monitorRect.top;
    if (width <= 0 || height <= 0) {
        return {monitorRect.left, monitorRect.top};
    }
    const double rx = static_cast<double>(xNorm) / 65535.0;
    const double ry = static_cast<double>(yNorm) / 65535.0;

    int32_t px = monitorRect.left + static_cast<int32_t>(std::llround(rx * static_cast<double>(width - 1)));
    int32_t py = monitorRect.top + static_cast<int32_t>(std::llround(ry * static_cast<double>(height - 1)));
    px = std::clamp(px, monitorRect.left, monitorRect.right - 1);
    py = std::clamp(py, monitorRect.top, monitorRect.bottom - 1);
    return {px, py};
}

PixelPoint mapPixelToVirtualDesk(
    int32_t px,
    int32_t py,
    const DisplayRect& virtualDesktopBounds) {
    const int64_t vw = static_cast<int64_t>(virtualDesktopBounds.right) - virtualDesktopBounds.left;
    const int64_t vh = static_cast<int64_t>(virtualDesktopBounds.bottom) - virtualDesktopBounds.top;
    if (vw <= 1 || vh <= 1) {
        return {0, 0};
    }
    const double nx = (static_cast<double>(px - virtualDesktopBounds.left) * 65535.0) / static_cast<double>(vw - 1);
    const double ny = (static_cast<double>(py - virtualDesktopBounds.top) * 65535.0) / static_cast<double>(vh - 1);
    return {
        std::clamp(static_cast<int32_t>(std::llround(nx)), 0, 65535),
        std::clamp(static_cast<int32_t>(std::llround(ny)), 0, 65535)
    };
}

} // namespace aegis::input
