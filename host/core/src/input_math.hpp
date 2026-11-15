#pragma once

#include <cstdint>

namespace aegis::input {

struct DisplayRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

struct PixelPoint {
    int32_t x;
    int32_t y;
};

// Maps normalized [0..65535] coordinates inside the virtual monitor to desktop pixel coordinates.
PixelPoint mapNormToMonitorPixel(uint16_t xNorm, uint16_t yNorm, const DisplayRect& monitorRect);

// Maps pixel coordinates within the virtual desktop bounding box into [0..65535] for MOUSEEVENTF_VIRTUALDESK.
PixelPoint mapPixelToVirtualDesk(
    int32_t px,
    int32_t py,
    const DisplayRect& virtualDesktopBounds);

} // namespace aegis::input
