#pragma once
#include <array>

namespace fm1 {
struct TouchPoint { float x, y; };

// Standard-layout, trivially copyable calibration record for Preferences.
// Version the containing storage record before changing this layout.
struct TouchCalibration {
    float ax = 0, bx = 0, cx = 0;
    float ay = 0, by = 0, cy = 0;
    bool valid = false;

    bool fit(const std::array<TouchPoint, 3>& raw,
             const std::array<TouchPoint, 3>& screen);
    // Returns NaNs for an invalid calibration/input. The caller clamps screen output.
    TouchPoint map(float rawX, float rawY) const;
    // Euclidean screen-pixel error of an independent fourth calibration point.
    bool verify(TouchPoint raw, TouchPoint target, float tolerance = 16) const;
};
} // namespace fm1
