#include "UiTypes.h"

namespace adv_walkman {
namespace player {

const char* uiPageName(UiPage page) {
    switch (page) {
        case UiPage::Player:
            return "PLAYER";
        case UiPage::Playlist:
            return "PLAYLIST";
        case UiPage::Library:
            return "LIBRARY";
        case UiPage::Settings:
            return "SETTINGS";
    }
    return "UNKNOWN";
}

const char* uiActionName(UiAction action) {
    switch (action) {
        case UiAction::None:
            return "NONE";
        case UiAction::Up:
            return "UP";
        case UiAction::Down:
            return "DOWN";
        case UiAction::Left:
            return "LEFT";
        case UiAction::Right:
            return "RIGHT";
        case UiAction::Confirm:
            return "CONFIRM";
        case UiAction::Back:
            return "BACK";
        case UiAction::OpenSettings:
            return "SETTINGS";
        case UiAction::ToggleView:
            return "VIEW";
    }
    return "UNKNOWN";
}

}  // namespace player
}  // namespace adv_walkman
