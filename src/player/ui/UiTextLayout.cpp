#include "UiTextLayout.h"

#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {

// On invalid input consume exactly one byte and substitute '?'. All valid
// units retain their byte length, so sanitized line offsets still map to the
// original input without a second buffer or per-character offset table.
size_t utf8Unit(const char* text, size_t remaining, bool& valid) {
    const auto first = static_cast<uint8_t>(text[0]);
    size_t count = 1;
    uint32_t codepoint = first;
    uint32_t minimum = 0;
    if (first < 0x80) {
        return 1;
    } else if (first >= 0xC2 && first <= 0xDF) {
        count = 2;
        codepoint = first & 0x1F;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        count = 3;
        codepoint = first & 0x0F;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        count = 4;
        codepoint = first & 0x07;
        minimum = 0x10000;
    } else {
        valid = false;
        return 1;
    }
    if (count > remaining) {
        valid = false;
        return 1;
    }
    for (size_t i = 1; i < count; ++i) {
        const auto byte = static_cast<uint8_t>(text[i]);
        if ((byte & 0xC0) != 0x80) {
            valid = false;
            return 1;
        }
        codepoint = (codepoint << 6) | (byte & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        valid = false;
        return 1;
    }
    return count;
}

bool horizontalSpace(char c) {
    return c == ' ' || c == '\t';
}

bool newline(char c) {
    return c == '\n' || c == '\r';
}

bool hasVisibleText(const char* text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (!horizontalSpace(text[i]) && !newline(text[i])) {
            return true;
        }
    }
    return false;
}

size_t boundaryAtOrBefore(const char* text, size_t length, size_t limit) {
    size_t offset = 0;
    while (offset < length) {
        bool valid = true;
        const size_t count = utf8Unit(text + offset, length - offset, valid);
        if (offset + count > limit) {
            break;
        }
        offset += count;
    }
    return offset;
}

int32_t measuredWidth(lgfx::LovyanGFX& display, const char* text) {
    // M5GFX's UTF-8 decoder is shared by drawing and measuring. Start each
    // operation at a complete codepoint, including after malformed input.
    display.setCursor(0, 0);
    return display.textWidth(text);
}

int32_t prefixWidth(lgfx::LovyanGFX& display, char* text, size_t bytes) {
    const char saved = text[bytes];
    text[bytes] = '\0';
    const int32_t width = measuredWidth(display, text);
    text[bytes] = saved;
    return width;
}

size_t fittingPrefix(lgfx::LovyanGFX& display, char* text, size_t bytes, int32_t width) {
    if (width < 0) {
        return 0;
    }
    display.setCursor(0, 0);
    // M5GFX 0.2.27 textLength stops at >= width; +1 admits an exact pixel fit.
    const int32_t fit = display.textLength(text, width + 1);
    size_t result = boundaryAtOrBefore(
        text, bytes, std::min(bytes, static_cast<size_t>(std::max(0, fit))));
    while (result != 0 && prefixWidth(display, text, result) > width) {
        result = boundaryAtOrBefore(text, bytes, result - 1);
    }
    return result;
}

size_t preferredBreak(lgfx::LovyanGFX& display, char* text, size_t bytes, size_t fit,
                      int32_t width) {
    size_t preferred = 0;
    for (size_t i = 0; i < fit; ++i) {
        const char c = text[i];
        if (c == ' ') {
            preferred = i;
        } else if (c == '/' || c == '-' || c == '_') {
            preferred = i + 1;
        }
        if (i + 1 <= fit && i + 1 < bytes && c >= 'a' && c <= 'z' &&
            text[i + 1] >= 'A' && text[i + 1] <= 'Z') {
            preferred = i + 1;
        }
    }
    return preferred != 0 && prefixWidth(display, text, preferred) * 4 >= width * 3
               ? preferred
               : fit;
}

void appendEllipsis(lgfx::LovyanGFX& display, char* line, size_t& bytes, int32_t width,
                    UiTextLayoutResult& result) {
    const int32_t dotsWidth = measuredWidth(display, "...");
    if (dotsWidth > width) {
        // A too-small box must not paint an over-wide ellipsis either.
        line[0] = '\0';
        bytes = 0;
        result.layoutError = true;
        return;
    }
    bytes = fittingPrefix(display, line, bytes, width - dotsWidth);
    bytes = boundaryAtOrBefore(line, std::strlen(line),
                               std::min(bytes, UiTextLayout::kLineCapacity - 4));
    while (bytes != 0 && line[bytes - 1] == ' ') {
        --bytes;
    }
    std::memcpy(line + bytes, "...", 4);
    while (bytes != 0 && measuredWidth(display, line) > width) {
        bytes = boundaryAtOrBefore(line, bytes, bytes - 1);
        std::memcpy(line + bytes, "...", 4);
    }
    bytes += 3;
}

UiTextLayoutResult layout(lgfx::LovyanGFX& display, const char* text,
                           const UiTextBox& box, size_t byteLength, bool paint) {
    UiTextLayoutResult result;
    result.availableWidthPx = std::max<int16_t>(0, box.width);
    if (text == nullptr) {
        text = "";
    }
    size_t length = 0;
    while (length < byteLength && text[length] != '\0') {
        ++length;
    }
    for (size_t i = 0; i < length;) {
        bool valid = true;
        i += utf8Unit(text + i, length - i, valid);
        result.invalidUtf8 |= !valid;
    }

    const int32_t fontHeight = display.fontHeight(display.getFont());
    if (box.width <= 0 || box.height < fontHeight || fontHeight <= 0 ||
        box.maxLines == 0 || box.lineGap < 0 || box.x < 0 || box.y < 0 ||
        box.x + box.width > display.width() ||
        box.y + box.height > display.height()) {
        result.layoutError = true;
        result.truncated = length != 0;
        return result;
    }
    const int32_t step = fontHeight + box.lineGap;
    const int32_t maxLines = std::min<int32_t>(
        box.maxLines, 1 + (box.height - fontHeight) / step);
    const auto savedStyle = display.getTextStyle();
    auto style = savedStyle;
    style.utf8 = true;
    style.datum = lgfx::textdatum_t::top_left;
    style.padding_x = 0;
    display.setTextStyle(style);
    display.setTextWrap(false);
    const int32_t savedX = display.getCursorX();
    const int32_t savedY = display.getCursorY();
    if (paint) {
        display.setClipRect(box.x, box.y, box.width, box.height);
    }

    size_t offset = 0;
    for (int32_t row = 0; row < maxLines && offset < length; ++row) {
        char line[UiTextLayout::kLineCapacity] = {};
        size_t bytes = 0;
        while (offset + bytes < length && !newline(text[offset + bytes])) {
            bool valid = true;
            const size_t count = utf8Unit(text + offset + bytes,
                                          length - offset - bytes, valid);
            if (bytes + count >= sizeof(line)) {
                break;
            }
            if (!valid || (count == 1 &&
                           static_cast<uint8_t>(text[offset + bytes]) < 0x20 &&
                           text[offset + bytes] != '\t')) {
                line[bytes] = '?';
            } else if (text[offset + bytes] == '\t') {
                line[bytes] = ' ';
            } else {
                std::memcpy(line + bytes, text + offset + bytes, count);
            }
            bytes += count;
        }
        const size_t fit = fittingPrefix(display, line, bytes, box.width);
        const bool atLineEnd = offset + bytes == length ||
                               newline(text[offset + bytes]);
        const bool lastLine = row + 1 == maxLines;
        size_t consumed = fit;
        if (!lastLine && (fit < bytes || !atLineEnd)) {
            consumed = preferredBreak(display, line, bytes, fit, box.width);
        }
        size_t next = offset + consumed;
        while (next < length && horizontalSpace(text[next])) {
            ++next;
        }
        if (next < length && newline(text[next])) {
            const char c = text[next++];
            if (c == '\r' && next < length && text[next] == '\n') {
                ++next;
            }
        }
        // Do not loop on a glyph that cannot fit even in an otherwise empty
        // line. Report the bad box and render at most an in-bounds ellipsis.
        const bool cannotFit = consumed == 0 && bytes != 0 && next == offset;
        size_t shown = consumed;
        while (shown != 0 && line[shown - 1] == ' ') {
            --shown;
        }
        line[shown] = '\0';
        if ((lastLine || cannotFit) &&
            hasVisibleText(text + next, length - next)) {
            result.truncated = true;
            if (box.ellipsizeLastLine) {
                appendEllipsis(display, line, shown, box.width, result);
            }
        }
        result.layoutError |= cannotFit;
        const int32_t width = measuredWidth(display, line);
        result.maxLineWidthPx = static_cast<int16_t>(
            std::max<int32_t>(result.maxLineWidthPx, width));
        result.layoutError |= width > box.width;
        if (paint) {
            display.setCursor(box.x, box.y + row * step);
            display.drawString(line, box.x, box.y + row * step);
        }
        ++result.lineCount;
        offset = next;
        if (cannotFit) {
            break;
        }
    }
    if (paint) {
        display.clearClipRect();
    }
    display.setTextStyle(savedStyle);
    display.setCursor(savedX, savedY);
    return result;
}

}  // namespace

UiTextLayoutResult UiTextLayout::draw(lgfx::LovyanGFX& display, const char* text,
                                     const UiTextBox& box, size_t byteLength) {
    return layout(display, text, box, byteLength, true);
}

UiTextLayoutResult UiTextLayout::measure(lgfx::LovyanGFX& display, const char* text,
                                        const UiTextBox& box, size_t byteLength) {
    return layout(display, text, box, byteLength, false);
}

namespace {
int32_t singleLine(lgfx::LovyanGFX& display, const char* text,
                    int32_t x, int32_t y, bool paint) {
    if (text == nullptr) return 0;
    const auto savedStyle = display.getTextStyle();
    const int32_t savedX = display.getCursorX();
    const int32_t savedY = display.getCursorY();
    auto style = savedStyle;
    style.utf8 = true;
    style.datum = lgfx::textdatum_t::top_left;
    style.padding_x = 0;
    display.setTextStyle(style);
    display.setTextWrap(false);
    const size_t length = std::strlen(text);
    int32_t width = 0;
    for (size_t offset = 0; offset < length;) {
        char chunk[UiTextLayout::kLineCapacity] = {};
        size_t count = 0;
        while (offset < length) {
            bool valid = true;
            const size_t bytes = utf8Unit(text + offset, length - offset, valid);
            if (count + bytes >= sizeof(chunk)) break;
            if (!valid || static_cast<uint8_t>(text[offset]) < 0x20) {
                chunk[count++] = valid ? ' ' : '?';
            } else {
                std::memcpy(chunk + count, text + offset, bytes);
                count += bytes;
            }
            offset += bytes;
        }
        const int32_t chunkWidth = measuredWidth(display, chunk);
        if (paint) display.drawString(chunk, x + width, y);
        width += chunkWidth;
    }
    display.setTextStyle(savedStyle);
    display.setCursor(savedX, savedY);
    return width;
}
}  // namespace

int32_t UiTextLayout::singleLineWidth(lgfx::LovyanGFX& display,
                                      const char* text) {
    return singleLine(display, text, 0, 0, false);
}

void UiTextLayout::drawScrolledLine(lgfx::LovyanGFX& display, const char* text,
                                    const UiTextBox& box, int32_t offsetPx) {
    if (box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0 ||
        box.x + box.width > display.width() ||
        box.y + box.height > display.height()) return;
    display.setClipRect(box.x, box.y, box.width, box.height);
    singleLine(display, text, box.x - std::max<int32_t>(0, offsetPx), box.y, true);
    display.clearClipRect();
}

void UiTextLayout::drawClippedLabel(lgfx::LovyanGFX& display, const char* text,
                                    int16_t x, int16_t y, int16_t width,
                                    size_t byteLength) {
    if (text == nullptr || width <= 0 || x < 0 || x + width > display.width()) return;
    const auto savedStyle = display.getTextStyle();
    const int32_t savedX = display.getCursorX(), savedY = display.getCursorY();
    auto style = savedStyle;
    style.utf8 = true;
    style.datum = lgfx::textdatum_t::top_left;
    style.padding_x = 0;
    display.setTextStyle(style);
    display.setTextWrap(false);
    const size_t length = std::min(std::strlen(text), byteLength);
    char line[kLineCapacity] = {};
    size_t bytes = 0;
    while (bytes < length) {
        bool valid = true;
        const size_t count = utf8Unit(text + bytes, length - bytes, valid);
        if (bytes + count >= sizeof(line)) break;
        if (!valid || static_cast<uint8_t>(text[bytes]) < 0x20) line[bytes] = '?';
        else std::memcpy(line + bytes, text + bytes, count);
        bytes += count;
    }
    if (bytes < length || measuredWidth(display, line) > width) {
        UiTextLayoutResult result;
        appendEllipsis(display, line, bytes, width, result);
    }
    display.setClipRect(x, 0, width, display.height());
    display.drawString(line, x, y);
    display.clearClipRect();
    display.setTextStyle(savedStyle);
    display.setCursor(savedX, savedY);
}

}  // namespace player
}  // namespace adv_walkman
