#pragma once

#include <cstddef>
#include <cstdint>

namespace adv_walkman { namespace player {

// Owns the retained six-row Playlist window and its incremental redraw state.
// Kept as a private implementation base of UiCoordinator so the refactor adds
// no pointer/reference members to the very tight UI state budget.
class PlaylistPageController {
 protected:
    size_t playlistSelected_ = 0;
    size_t savedPlaylistSelected_ = 0;
    size_t locateIndex_ = 0;
    uint32_t selectionRequestedAt_ = 0;
    uint8_t metadataReadyRows_ = 0;
    uint8_t dirtyRegions_ = 255;
    uint8_t feedbackRegions_ = 0;
    uint8_t prepareRow_ = 0;
    uint8_t drawRegion_ = 0;
    bool metadataRequested_ = false;
    bool retainedPlaylist_ = false;
    bool locateCurrent_ = false;
    bool warmReturnPending_ = false;
};

} }
