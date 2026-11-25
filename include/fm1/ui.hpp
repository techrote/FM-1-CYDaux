#pragma once
#include "fm1/model.hpp"
#include <cstddef>
#include <cstdint>

namespace fm1 {
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xf8u) << 8u) | ((g & 0xfcu) << 3u) | (b >> 3u));
}
struct Rect {
  int x, y, w, h;
  constexpr bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
// The common renderer owns fonts, clipping, geometry and input hit testing.
class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
};
namespace theme {
constexpr uint16_t background = rgb565(13, 19, 28);
constexpr uint16_t panel = rgb565(24, 35, 48);
constexpr uint16_t panelActive = rgb565(30, 53, 62);
constexpr uint16_t line = rgb565(45, 61, 77);
constexpr uint16_t text = rgb565(232, 239, 247);
constexpr uint16_t muted = rgb565(145, 164, 184);
constexpr uint16_t accent = rgb565(66, 222, 184);
constexpr uint16_t amber = rgb565(246, 188, 100);
constexpr uint16_t danger = rgb565(250, 113, 139);
constexpr uint16_t blue = rgb565(119, 175, 255);
}
void drawText(Canvas& canvas, int x, int y, const char* text,
              uint16_t color = theme::text, int scale = 1, std::size_t maxChars = 52);
int textWidth(const char* text, int scale = 1, std::size_t maxChars = 52);
void drawPanel(Canvas& canvas, Rect rect, uint16_t fill, uint16_t stroke = theme::line);
enum class TouchPhase : uint8_t { Down, Move, Up, Cancel };
using ActionCallback = void (*)(void* context, UiAction action);
// One finger, one captured target. Drags cannot become profile/tab changes.
class Ui {
 public:
  explicit Ui(ActionCallback callback, void* context = nullptr)
      : callback_(callback), context_(context) {}
  void render(Canvas& canvas, const UiModel& model) const;
  void touch(TouchPhase phase, int x, int y, const UiModel& model);
  void cancelTouch();
 private:
  enum class Gesture : uint8_t { None, Button, Control, Level, XY, Note };
  ActionCallback callback_ = nullptr;
  void* context_ = nullptr;
  Gesture gesture_ = Gesture::None;
  Rect capture_{0, 0, 0, 0};
  UiAction button_{ActionType::Refresh};
  int index_ = 0, lastA_ = -1, lastB_ = -1;
  Page page_ = Page::Play;
  uint8_t profile_ = 0, track_ = 0, bank_ = 0;
  TransportKind transport_ = TransportKind::Demo;
  ConnectionState connection_ = ConnectionState::Offline;
  bool setup_ = false;
  void emit(UiAction action) const;
  void drag(int x, int y);
};
} // namespace fm1
