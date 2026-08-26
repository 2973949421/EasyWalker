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
    display.setTextSize(1.0f);
    display.setTextColor(kAccent, kBackground);
    display.setCursor(7, 7);
    display.print(title);
    display.drawFastHLine(6, 23, display.width() - 12, kMuted);
}

void footer(M5GFX& display, const char* hint) {
    display.fillRect(0, display.height() - 24, display.width(), 24, kPanel);
    display.setTextColor(kText, kPanel);
    display.setTextSize(0.8f);
    display.setCursor(5, display.height() - 18);
    display.print(hint == nullptr || hint[0] == '\0' ? "" : hint);
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
    beginPage(display, "ADV WALKMAN / LIBRARY");
    display.fillRoundRect(10, 39, display.width() - 20, 120, 8, kPanel);
    display.drawRoundRect(10, 39, display.width() - 20, 120, 8, kAccent);
    display.setTextColor(kMuted, kPanel);
    display.setTextSize(0.9f);
    display.setCursor(19, 53);
    display.print("P3A library card");
    display.setTextColor(kText, kPanel);
    display.setTextSize(1.25f);
    display.setCursor(19, 82);
    display.printf("%.17s", context.libraryName == nullptr
                                 ? "Loading..."
                                 : context.libraryName);
    display.setTextSize(0.9f);
    display.setTextColor(kAccent, kPanel);
    display.setCursor(19, 126);
    display.printf("%u / %u",
                   static_cast<unsigned>(context.catalogCount == 0
                                             ? 0
                                             : context.catalogIndex + 1),
                   static_cast<unsigned>(context.catalogCount));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(11, 174);
    display.print("Fn+Left/Right   Enter");
    if (context.error != nullptr && context.error[0] != '\0') {
        display.setTextColor(TFT_ORANGE, kBackground);
        display.setCursor(11, 193);
        display.printf("%.25s", context.error);
    }
    footer(display, context.hint == nullptr ? "S Settings  Fn+Esc no-op"
                                            : context.hint);
}

void PlaylistPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, context.libraryName == nullptr ? "PLAYLIST"
                                                       : context.libraryName);
    display.setTextSize(0.72f);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 28);
    display.printf("%.32s", context.directoryPath == nullptr
                               ? "Loading..."
                               : context.directoryPath);

    constexpr int rowTop = 43;
    constexpr int rowHeight = 23;
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
        display.setTextSize(0.88f);
        display.setCursor(7, y + 6);
        display.printf("%c%c %.22s",
                       row.playing ? '>' : ' ',
                       row.type == LibraryEntryType::Directory ? '/' : ' ',
                       row.label);
    }
    if (context.playlistCount == 0 && context.error == nullptr) {
        display.setTextColor(kMuted, kBackground);
        display.setCursor(12, 90);
        display.print("No playable entries");
    }
    footer(display, context.hint == nullptr ? "Fn+Up/Down Enter Fn+Esc"
                                            : context.hint);
}

void PlayerPageRenderer::render(M5GFX& display,
                                const UiRenderContext& context) {
    beginPage(display, "ADV WALKMAN / PLAYER");
    display.fillRoundRect(10, 42, display.width() - 20, 126, 7, kPanel);
    display.setTextColor(kMuted, kPanel);
    display.setTextSize(0.9f);
    display.setCursor(18, 55);
    display.print("P3A player placeholder");
    display.setTextColor(kText, kPanel);
    display.setTextSize(1.15f);
    display.setCursor(18, 83);
    display.printf("%.18s", basenameOf(context.currentTrack));
    display.setTextSize(0.95f);
    display.setTextColor(kAccent, kPanel);
    display.setCursor(18, 119);
    display.printf("%s  %lu.%03lus",
                   context.playerState == nullptr ? "EMPTY"
                                                  : context.playerState,
                   static_cast<unsigned long>(context.positionMs / 1000U),
                   static_cast<unsigned long>(context.positionMs % 1000U));
    display.setTextColor(kMuted, kBackground);
    display.setCursor(11, 186);
    display.print("Header/Footer arrive in P3B");
    footer(display, context.hint == nullptr ? "Fn+Esc Playlist"
                                            : context.hint);
}

void SettingsPageRenderer::render(M5GFX& display,
                                  const UiRenderContext& context) {
    beginPage(display, "ADV WALKMAN / SETTINGS");
    display.setTextColor(kText, kBackground);
    display.setTextSize(1.0f);
    display.setCursor(12, 50);
    display.print("P3A foundation");
    display.setCursor(12, 78);
    display.printf("Version %.20s", ADV_WALKMAN_VERSION);
    display.setTextColor(kMuted, kBackground);
    display.setCursor(12, 116);
    display.print("Brightness");
    display.setCursor(12, 139);
    display.print("Screen timeout");
    display.setCursor(12, 162);
    display.print("Launcher return");
    display.setTextColor(kAccent, kBackground);
    display.setCursor(12, 190);
    display.print("Functional UI: P3D");
    footer(display, context.hint == nullptr ? "Fn+Esc Library"
                                            : context.hint);
}

}  // namespace player
}  // namespace adv_walkman
