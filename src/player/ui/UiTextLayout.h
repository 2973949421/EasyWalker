#pragma once

#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

namespace adv_walkman {
namespace player {

struct UiTextBox {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t maxLines;
    int8_t lineGap;
    bool ellipsizeLastLine;
};

struct UiTextLayoutResult {
    uint8_t lineCount = 0;
    int16_t maxLineWidthPx = 0;
    int16_t availableWidthPx = 0;
    bool truncated = false;
    bool invalidUtf8 = false;
    bool layoutError = false;
};

class UiTextLayout final {
  public:
    static constexpr size_t kLineCapacity = 128;

    // Uses the active font/size/colours, left aligned. byteLength permits a
    // bounded view (e.g. the first line of a two-colour Footer), not a width
    // estimate. No heap allocation, source edits or automatic text wrapping.
    static UiTextLayoutResult draw(lgfx::LovyanGFX& display, const char* text,
                                   const UiTextBox& box,
                                   size_t byteLength = SIZE_MAX);
    // Same layout and metrics without drawing or changing the clip rectangle.
    static UiTextLayoutResult measure(lgfx::LovyanGFX& display, const char* text,
                                      const UiTextBox& box,
                                      size_t byteLength = SIZE_MAX);
    // Whole single-line text, processed in UTF-8-safe fixed chunks. Unlike
    // draw(), this never ellipsizes at the 128-byte scratch boundary.
    static int32_t singleLineWidth(lgfx::LovyanGFX& display, const char* text);
    static void drawScrolledLine(lgfx::LovyanGFX& display, const char* text,
                                 const UiTextBox& box, int32_t offsetPx);
    // Ellipsized line for stripe rendering. y may be negative; clipping stays
    // inside the target surface. Width is still measured in actual pixels.
    static void drawClippedLabel(lgfx::LovyanGFX& display, const char* text,
                                 int16_t x, int16_t y, int16_t width,
                                 size_t byteLength = SIZE_MAX);
};

}  // namespace player
}  // namespace adv_walkman
