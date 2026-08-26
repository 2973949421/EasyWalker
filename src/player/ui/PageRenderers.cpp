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

void beginPage(M5GFX& display, const char* title) {
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
    display.setCursor(7, top + 6);
    if (newline == nullptr) {
        display.printf("%.17s", value);
        return;
    }
    display.printf("%.*s", static_cast<int>(newline - value), value);
    display.setTextColor(kText, kPanel);
    display.setCursor(7, top + 24);
    display.printf("%.17s", newline + 1);
}

const char* basenameOf(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return "none";
    }
    const char* slash = std::strrchr(path, '/');
    return slash == nullptr ? path : slash + 1;
}

}  // namespace

void LibraryPageRenderer::render(M5GFX& display,
                                 const UiRenderContext& context) {
    beginPage(display, "LIBRARY");
    display.fillRoundRect(10, 39, display.width() - 20, 112, 8, kPanel);
    display.drawRoundRect(10, 39, display.width() - 20, 112, 8, kAccent);
    display.setTextColor(kMuted, kPanel);
    display.setTextSize(1.05f);
    display.setCursor(19, 53);
    display.print("COLLECTION");
    display.setTextColor(kText, kPanel);
    display.setTextSize(1.5f);
    display.setCursor(19, 82);
    display.printf("%.17s", context.libraryName == nullptr
                                 ? "Loading..."
                                 : context.libraryName);
    display.setTextSize(1.15f);
    display.setTextColor(kAccent, kPanel);
    display.setCursor(19, 122);
    display.printf("%u / %u",
                   static_cast<unsigned>(context.catalogCount == 0
                                             ? 0
                                             : context.catalogIndex + 1),
                   static_cast<unsigned>(context.catalogCount));
    display.setTextSize(1.05f);
    display.setTextColor(kText, kBackground);
    display.setCursor(11, 161);
    display.print("LEFT / RIGHT");
    display.setCursor(11, 178);
    display.print("ENTER TO OPEN");
    if (context.error != nullptr && context.error[0] != '\0') {
        display.setTextColor(TFT_ORANGE, kBackground);
        display.setCursor(11, 178);
        display.printf("%.17s", context.error);
    }
    footer(display, context.hint == nullptr ? "S: SETTINGS\nESC: STAY"
                                            : context.hint);
}

void PlaylistPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, "PLAYLIST");
    display.setTextSize(1.0f);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 28);
    display.printf("%.20s", context.libraryName == nullptr
                               ? "Loading..."
                               : context.libraryName);

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
        display.setTextSize(1.05f);
        display.setCursor(7, y + 7);
        display.printf("%c%c %.17s",
                       row.playing ? '>' : ' ',
                       row.type == LibraryEntryType::Directory ? '/' : ' ',
                       row.label);
    }
    if (context.playlistCount == 0 && context.error == nullptr) {
        display.setTextColor(kMuted, kBackground);
        display.setCursor(12, 90);
        display.print("No playable entries");
    }
    footer(display, context.hint == nullptr ? "UP/DOWN + ENTER\nESC: BACK"
                                            : context.hint);
}

void PlayerPageRenderer::render(M5GFX& display,
                                const UiRenderContext& context) {
    beginPage(display, "NOW PLAYING");
    display.fillRoundRect(10, 42, display.width() - 20, 126, 7, kPanel);
    display.setTextColor(kMuted, kPanel);
    display.setTextSize(1.1f);
    display.setCursor(18, 55);
    display.print("PLAYING");
    display.setTextColor(kText, kPanel);
    display.setTextSize(1.35f);
    display.setCursor(18, 83);
    display.printf("%.18s", basenameOf(context.currentTrack));
    display.setTextSize(1.15f);
    display.setTextColor(kAccent, kPanel);
    display.setCursor(18, 119);
    display.printf("%s  %lu.%03lus",
                   context.playerState == nullptr ? "EMPTY"
                                                  : context.playerState,
                   static_cast<unsigned long>(context.positionMs / 1000U),
                   static_cast<unsigned long>(context.positionMs % 1000U));
    footer(display, context.hint == nullptr ? "FN+ESC\nBACK TO LIST"
                                            : context.hint);
}

void SettingsPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, "SETTINGS");
    display.setTextColor(kText, kBackground);
    display.setTextSize(1.35f);
    display.setCursor(12, 50);
    display.print("P3A FOUNDATION");
    display.setCursor(12, 78);
    display.setTextSize(1.0f);
    display.printf("%.20s", ADV_WALKMAN_VERSION);
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
