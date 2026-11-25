#include "fm1/touch.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using fm1::TouchCalibration;
using fm1::TouchPoint;
using Triangle = std::array<TouchPoint, 3>;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "touch: " << message << '\n'; std::exit(1); }
}
bool near(TouchPoint a, TouchPoint b, float error = 0.02f) {
    return std::abs(a.x - b.x) < error && std::abs(a.y - b.y) < error;
}
TouchPoint expected(TouchPoint raw, unsigned orientation) {
    const float u = (raw.x - 200) / 3500, v = (raw.y - 300) / 3300;
    switch (orientation) {
        case 1: return {20 + 280 * v, 220 - 200 * u}; // 90 degrees
        case 2: return {300 - 280 * u, 220 - 200 * v}; // 180 degrees
        case 3: return {300 - 280 * v, 20 + 200 * u}; // 270 degrees
        case 4: return {300 - 280 * u, 20 + 200 * v}; // X flip
        case 5: return {20 + 280 * u, 220 - 200 * v}; // Y flip
        case 6: return {20 + 280 * v, 20 + 200 * u}; // swapped ADC axes
        default: return {20 + 280 * u, 20 + 200 * v};
    }
}
const Triangle raw{{{200, 300}, {3700, 300}, {200, 3600}}};
const Triangle screen{{{20, 20}, {300, 20}, {20, 220}}};

void testOrientations() {
    for (unsigned orientation = 0; orientation < 7; ++orientation) {
        Triangle targets;
        for (std::size_t i = 0; i < 3; ++i) targets[i] = expected(raw[i], orientation);
        TouchCalibration c;
        check(c.fit(raw, targets) && c.valid, "normal / rotation / reflection fit");
        for (std::size_t i = 0; i < 3; ++i)
            check(near(c.map(raw[i].x, raw[i].y), targets[i]), "fitted points");
        const TouchPoint fourth{3700, 3600};
        check(c.verify(fourth, expected(fourth, orientation), 0.02f), "independent fourth corner");
        const TouchPoint interior{1495, 2346};
        check(near(c.map(interior.x, interior.y), expected(interior, orientation)), "interior interpolation");
    }
}

void testRejection() {
    TouchCalibration c;
    check(c.fit(raw, screen), "baseline fit");
    auto bad = raw;
    bad[2] = bad[0];
    check(!c.fit(bad, screen) && !c.valid, "duplicate points invalidate old calibration");
    bad = {{{100, 100}, {2000, 2000}, {3000, 3000}}};
    check(!c.fit(bad, screen), "collinear raw points");
    bad[2].y += 0.01f;
    check(!c.fit(bad, screen), "nearly collinear raw points");
    bad = {{{0, 0}, {0.1f, 0}, {0, 0.1f}}};
    check(!c.fit(bad, screen), "near-zero raw determinant");
    bad = raw;
    bad[0].x = -1;
    check(!c.fit(bad, screen), "negative ADC input");
    bad[0].x = 4096;
    check(!c.fit(bad, screen), "ADC input above 12-bit range");
    bad = raw;
    bad[0].x = std::numeric_limits<float>::infinity();
    check(!c.fit(bad, screen), "infinite raw input");
    bad = raw;
    bad[1].y = std::numeric_limits<float>::quiet_NaN();
    check(!c.fit(bad, screen), "NaN raw input");
    auto badScreen = screen;
    badScreen[2].x = std::numeric_limits<float>::quiet_NaN();
    check(!c.fit(raw, badScreen), "NaN screen target");
    badScreen = {{{0, 0}, {100, 100}, {200, 200}}};
    check(!c.fit(raw, badScreen), "collinear screen targets");
    const auto point = c.map(1000, 1000);
    check(!std::isfinite(point.x) && !std::isfinite(point.y), "invalid map returns NaN");
    check(!c.verify({1000, 1000}, {100, 100}), "invalid calibration cannot verify");
}

void testVerificationAndStorage() {
    static_assert(std::is_standard_layout<TouchCalibration>::value, "Preferences record layout");
    static_assert(std::is_trivially_copyable<TouchCalibration>::value, "Preferences byte persistence");
    TouchCalibration c;
    check(c.fit(raw, screen), "valid fit for storage");
    const TouchPoint fourth{3700, 3600};
    check(c.verify(fourth, {300, 220}), "fourth-point default tolerance");
    check(c.verify(fourth, {309, 229}), "Euclidean error within tolerance");
    check(!c.verify(fourth, {312, 232}), "Euclidean error beyond tolerance");
    check(!c.verify(fourth, {317, 220}), "wrong fourth-point location");
    check(!c.verify(fourth, {300, 220}, -1), "negative tolerance");
    check(!c.verify(fourth, {300, 220}, std::numeric_limits<float>::infinity()), "infinite tolerance");
    check(!c.verify({4096, 1000}, {300, 220}), "out-of-range verification raw input");
    check(!c.verify(fourth, {std::numeric_limits<float>::quiet_NaN(), 220}), "invalid verification target");
    const TouchPoint extrapolated = c.map(0, 0);
    check(extrapolated.x < 20 && extrapolated.y < 20, "map leaves screen clamping to caller");

    std::array<unsigned char, sizeof(TouchCalibration)> bytes{};
    std::memcpy(bytes.data(), &c, sizeof(c));
    TouchCalibration restored;
    std::memcpy(&restored, bytes.data(), sizeof(restored));
    check(restored.valid && restored.verify(fourth, {300, 220}, 0.02f), "Preferences byte round-trip");
    restored.ax = std::numeric_limits<float>::quiet_NaN();
    check(!restored.verify(fourth, {300, 220}), "nonfinite persisted coefficient rejected");
}
} // namespace

int main() {
    testOrientations();
    testRejection();
    testVerificationAndStorage();
    std::cout << "touch calibration tests passed\n";
}
