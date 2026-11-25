#include "fm1/touch.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace fm1 {
namespace {
bool finitePoint(TouchPoint point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}
bool adcPoint(TouchPoint point) {
    return finitePoint(point) && point.x >= 0 && point.x <= 4095 &&
           point.y >= 0 && point.y <= 4095;
}
double distanceSquared(TouchPoint a, TouchPoint b) {
    const double dx = double(a.x) - b.x, dy = double(a.y) - b.y;
    return dx * dx + dy * dy;
}
double triangleScale(const std::array<TouchPoint, 3>& points) {
    return std::max({distanceSquared(points[0], points[1]),
                     distanceSquared(points[1], points[2]),
                     distanceSquared(points[2], points[0])});
}
double determinant(const std::array<TouchPoint, 3>& points) {
    const double dx1 = double(points[1].x) - points[0].x;
    const double dy1 = double(points[1].y) - points[0].y;
    const double dx2 = double(points[2].x) - points[0].x;
    const double dy2 = double(points[2].y) - points[0].y;
    return dx1 * dy2 - dx2 * dy1;
}
bool finiteCoefficients(const TouchCalibration& c) {
    return std::isfinite(c.ax) && std::isfinite(c.bx) && std::isfinite(c.cx) &&
           std::isfinite(c.ay) && std::isfinite(c.by) && std::isfinite(c.cy);
}
} // namespace

bool TouchCalibration::fit(const std::array<TouchPoint, 3>& raw,
                           const std::array<TouchPoint, 3>& screen) {
    *this = {};
    for (std::size_t i = 0; i < 3; ++i)
        if (!adcPoint(raw[i]) || !finitePoint(screen[i])) return false;
    const double det = determinant(raw);
    // A skinny triangle amplifies touch noise. The relative test is independent
    // of rotation, reflection, and the raw ADC axis span.
    if (std::abs(det) <= std::max(1.0, triangleScale(raw) * 1e-4) ||
        std::abs(determinant(screen)) <= std::max(1e-6, triangleScale(screen) * 1e-6))
        return false;
    const double dx1 = double(raw[1].x) - raw[0].x;
    const double dy1 = double(raw[1].y) - raw[0].y;
    const double dx2 = double(raw[2].x) - raw[0].x;
    const double dy2 = double(raw[2].y) - raw[0].y;
    const double sx1 = double(screen[1].x) - screen[0].x;
    const double sx2 = double(screen[2].x) - screen[0].x;
    const double sy1 = double(screen[1].y) - screen[0].y;
    const double sy2 = double(screen[2].y) - screen[0].y;
    const double a = (sx1 * dy2 - sx2 * dy1) / det;
    const double b = (dx1 * sx2 - dx2 * sx1) / det;
    const double d = (sy1 * dy2 - sy2 * dy1) / det;
    const double e = (dx1 * sy2 - dx2 * sy1) / det;
    TouchCalibration fitted;
    fitted.ax = static_cast<float>(a);
    fitted.bx = static_cast<float>(b);
    fitted.cx = static_cast<float>(screen[0].x - a * raw[0].x - b * raw[0].y);
    fitted.ay = static_cast<float>(d);
    fitted.by = static_cast<float>(e);
    fitted.cy = static_cast<float>(screen[0].y - d * raw[0].x - e * raw[0].y);
    if (!finiteCoefficients(fitted)) return false;
    fitted.valid = true;
    *this = fitted;
    return true;
}

TouchPoint TouchCalibration::map(float rawX, float rawY) const {
    const TouchPoint raw{rawX, rawY};
    if (!valid || !adcPoint(raw) || !finiteCoefficients(*this)) {
        const float missing = std::numeric_limits<float>::quiet_NaN();
        return {missing, missing};
    }
    return {ax * rawX + bx * rawY + cx, ay * rawX + by * rawY + cy};
}

bool TouchCalibration::verify(TouchPoint raw, TouchPoint target, float tolerance) const {
    if (!valid || !finitePoint(target) || !std::isfinite(tolerance) || tolerance < 0)
        return false;
    const TouchPoint actual = map(raw.x, raw.y);
    return finitePoint(actual) &&
           distanceSquared(actual, target) <= double(tolerance) * tolerance;
}
} // namespace fm1
