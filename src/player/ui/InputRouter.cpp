#include "InputRouter.h"

#include <Arduino.h>
#include <M5Cardputer.h>

namespace adv_walkman {
namespace player {

namespace {

bool hasCoordinate(int x, int y) {
    const auto& keys = M5Cardputer.Keyboard.keyList();
    for (const Point2D_t& key : keys) {
        if (key.x == x && key.y == y) {
            return true;
        }
    }
    return false;
}

void capturePrimary(RawKeyEvent& raw) {
    const auto& keys = M5Cardputer.Keyboard.keyList();
    raw.keyCount = static_cast<uint8_t>(keys.size());
    for (const Point2D_t& key : keys) {
        // Prefer the non-Fn key when a combination is pressed.
        if (key.x == 0 && key.y == 2) {
            continue;
        }
        raw.x = static_cast<int8_t>(key.x);
        raw.y = static_cast<int8_t>(key.y);
        return;
    }
    if (!keys.empty()) {
        raw.x = static_cast<int8_t>(keys.front().x);
        raw.y = static_cast<int8_t>(keys.front().y);
    }
}

}  // namespace

bool InputRouter::poll(UiAction& action, RawKeyEvent& raw) {
    action = UiAction::None;
    raw = RawKeyEvent{};
    if (!M5Cardputer.Keyboard.isChange() ||
        !M5Cardputer.Keyboard.isPressed()) {
        return false;
    }

    const uint32_t now = millis();
    if (now - lastAcceptedAtMs_ < kDebounceMs) {
        return false;
    }

    const Keyboard_Class::KeysState& state = M5Cardputer.Keyboard.keysState();
    raw.fn = state.fn;
    capturePrimary(raw);

    if (state.fn) {
        // Printed ADV navigation positions:
        // Fn+` = Esc, Fn+; = Up, Fn+, = Left, Fn+. = Down, Fn+/ = Right.
        if (hasCoordinate(0, 0)) {
            action = UiAction::Back;
        } else if (hasCoordinate(11, 2)) {
            action = UiAction::Up;
        } else if (hasCoordinate(10, 3)) {
            action = UiAction::Left;
        } else if (hasCoordinate(11, 3)) {
            action = UiAction::Down;
        } else if (hasCoordinate(12, 3)) {
            action = UiAction::Right;
        }
    } else if (state.enter) {
        action = UiAction::Confirm;
    } else if (hasCoordinate(3, 2)) {
        action = UiAction::OpenSettings;
    }

    if (action == UiAction::None) {
        return false;
    }
    lastAcceptedAtMs_ = now;
    return true;
}

}  // namespace player
}  // namespace adv_walkman
