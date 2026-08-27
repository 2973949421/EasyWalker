#include "PageRenderers.h"

#include <cstdio>
#include <cstring>

namespace adv_walkman {
namespace player {

namespace {

constexpr uint16_t kBackground = 0x0861;
constexpr uint16_t kPanel = 0x10E3;
constexpr uint16_t kAccent = 0xFBE0;
constexpr uint16_t kMuted = 0x8410;
constexpr uint16_t kText = 0xFFFF;

UiTextBox textBox(int x, int y, int width, int height, uint8_t lines = 1) {
    return {static_cast<int16_t>(x), static_cast<int16_t>(y),
            static_cast<int16_t>(width), static_cast<int16_t>(height),
            lines, 3, true};
}

void beginPage(M5GFX& display, const char* title) {
    display.clearClipRect();
    display.fillScreen(kBackground);
    display.setTextWrap(false);
    display.setTextSize(1.3f);
    display.setTextColor(kAccent, kBackground);
    display.setCursor(7, 7);
    display.print(title);
    display.drawFastHLine(6, 25, display.width() - 12, kMuted);
}

void footer(M5GFX& display, const char* hint) {
    constexpr int footerHeight = 44;
    const int top = display.height() - footerHeight;
    display.fillRect(0, top, display.width(), footerHeight, kPanel);
    display.setTextSize(1.25f);
    const char* value = hint == nullptr ? "" : hint;
    const char* newline = std::strchr(value, '\n');
    display.setTextColor(kAccent, kPanel);
    if (newline == nullptr) {
        UiTextLayout::draw(display, value,
                           textBox(7, top + 6, display.width() - 14, 18));
        return;
    }
    UiTextLayout::draw(display, value,
                       textBox(7, top + 6, display.width() - 14, 18),
                       static_cast<size_t>(newline - value));
    display.setTextColor(kText, kPanel);
    UiTextLayout::draw(display, newline + 1,
                       textBox(7, top + 24, display.width() - 14, 18));
}

}  // namespace

UiTextLayoutResult LibraryPageRenderer::render(
    M5GFX& display, const UiRenderContext& context) {
    beginPage(display, "LIBRARY");
    display.fillRoundRect(10, 39, display.width() - 20, 112, 8, kPanel);
    display.drawRoundRect(10, 39, display.width() - 20, 112, 8, kAccent);
    display.setTextColor(kMuted, kPanel);
    display.setTextSize(1.05f);
    display.setCursor(19, 53);
    display.print("COLLECTION");
    display.setTextColor(kText, kPanel);
    display.setTextSize(1.5f);
    const UiTextLayoutResult nameLayout = UiTextLayout::draw(
        display, context.libraryName[0] == '\0' ? "Loading..."
                                                : context.libraryName,
        textBox(19, 76, display.width() - 38, 38, 2));
    display.setTextSize(1.15f);
    display.setTextColor(kAccent, kPanel);
    char count[32] = {};
    std::snprintf(count, sizeof(count), "%u / %u",
                  static_cast<unsigned>(context.catalogCount == 0
                                            ? 0
                                            : context.catalogIndex + 1),
                  static_cast<unsigned>(context.catalogCount));
    UiTextLayout::draw(display, count,
                       textBox(19, 122, display.width() - 38, 20));
    display.setTextSize(1.05f);
    display.setTextColor(kText, kBackground);
    display.setCursor(11, 161);
    display.print("LEFT / RIGHT");
    if (context.error != nullptr && context.error[0] != '\0') {
        display.setTextColor(TFT_ORANGE, kBackground);
        UiTextLayout::draw(display, context.error,
                           textBox(11, 178, display.width() - 22, 16));
    } else {
        display.setCursor(11, 178);
        display.print("ENTER TO OPEN");
    }
    footer(display, context.hint == nullptr ? "S: SETTINGS\nESC: STAY"
                                            : context.hint);
    return nameLayout;
}

void PlaylistPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, "PLAYLIST");
    display.setTextSize(1.5f);
    display.setTextColor(kMuted, kBackground);
    UiTextLayout::draw(display,
                       context.libraryName[0] == '\0' ? "Loading..."
                                                       : context.libraryName,
                       textBox(6, 28, display.width() - 12, 13));

    constexpr int rowTop = 43;
    constexpr int rowHeight = 25;
    for (size_t index = 0; index < kP3AVisibleRows; ++index) {
        const PlaylistRenderRow& row = context.rows[index];
        const int y = rowTop + static_cast<int>(index) * rowHeight;
        if (!row.valid) {
            continue;
        }
        const uint16_t background = row.selected ? kAccent : kBackground;
        const uint16_t foreground = row.selected ? kBackground : kText;
        display.fillRect(4, y, display.width() - 8, rowHeight - 2,
                         background);
        display.setTextColor(foreground, background);
        display.setTextSize(1.5f);
        const char marker[] = {
            row.playing ? '>' : ' ',
            row.type == LibraryEntryType::Directory ? '/' : ' ', ' ', '\0'};
        display.setCursor(7, y + 7);
        const int markerWidth = display.textWidth(marker);
        display.setCursor(7, y + 7);
        display.print(marker);
        UiTextLayout::draw(display, row.label,
                           textBox(7 + markerWidth, y + 7,
                                   display.width() - 14 - markerWidth, 15));
    }
    if (context.playlistCount == 0 && context.error == nullptr) {
        display.setTextColor(kMuted, kBackground);
        UiTextLayout::draw(display, "No playable entries",
                           textBox(12, 90, display.width() - 24, 32, 2));
    }
    footer(display, context.hint == nullptr ? "UP/DOWN + ENTER\nESC: BACK"
                                            : context.hint);
}

void SettingsPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, "SETTINGS");
    display.setTextColor(kText, kBackground);
    display.setTextSize(1.35f);
    display.setCursor(12, 50);
    display.print("P3A FOUNDATION");
    display.setTextSize(1.0f);
    UiTextLayout::draw(display, ADV_WALKMAN_VERSION,
                       textBox(12, 78, display.width() - 24, 20));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(12, 118);
    display.print("Full settings UI");
    display.setTextColor(kAccent, kBackground);
    display.setCursor(12, 143);
    display.print("arrives in P3D");
    footer(display, context.hint == nullptr ? "FN+ESC\nBACK TO LIBRARY"
                                            : context.hint);
}

}  // namespace player
}  // namespace adv_walkman
