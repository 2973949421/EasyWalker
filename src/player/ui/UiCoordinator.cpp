#include "UiCoordinator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "PlayerKeys.h"
#include "P3BChecks.h"

namespace adv_walkman {
namespace player {

bool UiCoordinator::begin(M5GFX& display, PlayerRuntime& player,
                          LibraryRuntime& libraryRuntime) {
    display_ = &display;
    player_ = &player;
    libraryRuntime_ = &libraryRuntime;
    nowPlaying_.begin();
    // Scratch-only checks before media binding / playback; no extra framebuffer.
    for (const auto result : {checkP3BModel(), checkP3BPresenterDrawing(nowPlaying_),
                               checkP3BOverlayRestoration(nowPlaying_)}) {
        stats_.displaySelfChecks += result.checks;
        if (!stats_.displaySelfFailure) stats_.displaySelfFailure = result.failure;
    }
    if(!fonts_.begin()) return false;
    nowPlaying_.bindMedia(fonts_);
    nowPlaying_.setPreferredView(player.preferredNowPlayingView());
    std::strcpy(libraryRoot_, MusicLibrary::kMusicRoot);
    std::strcpy(libraryName_, "Uncategorized");

    MusicLibrary& library = libraryRuntime.library();
    library.openRoot();
    const PlayerSnapshot snapshot = player.snapshot();
    if (snapshot.hasCurrent) {
        page_ = UiPage::Player;
        char track[kTrackPathCapacity] = {};
        if (player.currentPath(track, sizeof(track))) {
            parentDirectoryOfTrack(track, lastPlaylistPath_,
                                   sizeof(lastPlaylistPath_));
            firstLevelLibraryOfTrack(track, libraryRoot_,
                                     sizeof(libraryRoot_), uncategorized_);
            displayNameFromPath(libraryRoot_, libraryName_,
                                sizeof(libraryName_));
        }
    } else {
        page_ = UiPage::Library;
    }
    stats_.minimumHeap = ESP.getFreeHeap();
    dirty_ = true;
    return true;
}

void UiCoordinator::service() {
    if (display_ == nullptr || player_ == nullptr || libraryRuntime_ == nullptr) {
        return;
    }
    stats_.minimumHeap = std::min(stats_.minimumHeap, ESP.getFreeHeap());
    servicePendingIntent();
    servicePendingNavigation();
    serviceSelectedMetadata();

    MusicLibrary& library = libraryRuntime_->library();
    if (page_ == UiPage::Library && library.state() == LibraryState::Ready &&
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0) {
        catalog_.sync(library);
    }

    const PlayerSnapshot snapshot = player_->snapshot();
    char current[kTrackPathCapacity] = {};
    player_->currentPath(current, sizeof(current));
    const uint32_t now = millis();
    nowPlaying_.setActive(page_ == UiPage::Player, now);
    nowPlaying_.update(snapshot, current, *libraryRuntime_, now);
    nowPlaying_.setPreferredView(player_->preferredNowPlayingView());
    if(page_==UiPage::Player) nowPlaying_.serviceMedia();
    else fonts_.service();
    const char* error = externalError_;
    if (error[0] == '\0' && snapshot.error != PlayerError::None) {
        error = playerErrorName(snapshot.error);
    }
    if(error[0]=='\0' && (fonts_.stats().ioErrors || fonts_.stats().missing || fonts_.stats().capacityErrors))error=fonts_.failure().reason;
    nowPlaying_.setContent(hint_, error);
    if (library.state() != lastLibraryState_ ||
        library.currentGeneration() != lastLibraryGeneration_ ||
        snapshot.state != lastPlayerState_ ||
        std::strcmp(current, lastCurrentTrack_) != 0) {
        lastLibraryState_ = library.state();
        lastLibraryGeneration_ = library.currentGeneration();
        lastPlayerState_ = snapshot.state;
        setPath(lastCurrentTrack_, sizeof(lastCurrentTrack_), current);
        dirty_ = true;
    }

    if(gateCard_[0]) {
        if(gateCardDirty_) {
            display_->setFont(&fonts::Font0);display_->fillScreen(0x0861);
            display_->setTextColor(TFT_WHITE,0x0861);display_->setTextSize(1.75f);
            UiTextLayout::draw(*display_,gateCard_,{6,12,123,216,10,5,true});
            gateCardDirty_=false;
        }
        return;
    }
    if (page_ == UiPage::Player) {
        if (nowPlaying_.renderOne(*display_)) {
            lastRenderAtMs_ = now;
            ++stats_.renderCount;
            stats_.renderMaxUs = std::max(stats_.renderMaxUs,
                                          nowPlaying_.stats().renderMaxUs);
        }
        dirty_ = false;
        return;
    }
    if (dirty_ && now - lastRenderAtMs_ >= kMinimumRenderIntervalMs) {
        renderRetryRequested_ = false;
        render();
        lastRenderAtMs_ = now;
        dirty_ = renderRetryRequested_;
    }
}

bool UiCoordinator::handleAction(UiAction action) {
    if (action == UiAction::None || libraryRuntime_ == nullptr ||
        player_ == nullptr) {
        return false;
    }
    ++stats_.inputEvents;
    externalError_[0] = '\0';
    MusicLibrary& library = libraryRuntime_->library();

    switch (page_) {
        case UiPage::Library:
            if (action == UiAction::Left) {
                catalog_.move(-1);
                dirty_ = true;
                return true;
            }
            if (action == UiAction::Right) {
                catalog_.move(1);
                dirty_ = true;
                return true;
            }
            if (action == UiAction::Confirm) {
                return beginSelectedLibrary();
            }
            if (action == UiAction::OpenSettings) {
                setPage(UiPage::Settings);
                return true;
            }
            return action == UiAction::Back;

        case UiPage::Playlist: {
            const size_t count = playlistCount();
            if (action == UiAction::Up && count != 0) {
                playlistSelected_ = playlistSelected_ == 0
                                        ? count - 1
                                        : playlistSelected_ - 1;
                metadataRequested_ = false;
                dirty_ = true;
                return true;
            }
            if (action == UiAction::Down && count != 0) {
                playlistSelected_ = (playlistSelected_ + 1) % count;
                metadataRequested_ = false;
                dirty_ = true;
                return true;
            }
            if (action == UiAction::Confirm) {
                return activatePlaylistEntry();
            }
            if (action == UiAction::Back) {
                return returnFromPlaylist();
            }
            return false;
        }

        case UiPage::Player:
            if(action==UiAction::TogglePlayback){
                return player_->snapshot().state==PlayerState::Playing?player_->pause():player_->play();
            }
            if(action==UiAction::VolumeUp || action==UiAction::VolumeDown){
                player_->setVolume(adjustedVolume(player_->volume(),action==UiAction::VolumeUp?8:-8));
                notifyVolumeAdjusted(player_->volume(),millis());
                return true;
            }
            if(action==UiAction::ToggleView) {
                const bool changed=nowPlaying_.toggleView();
                if(changed)player_->setPreferredNowPlayingView(static_cast<uint8_t>(nowPlaying_.mediaStatus().preferred));
                return changed;
            }
            if (action == UiAction::Back) {
                return restorePlaylistForCurrentTrack();
            }
            return false;

        case UiPage::Settings:
            if (action == UiAction::Back) {
                library.openRoot();
                setPage(UiPage::Library);
                return true;
            }
            return false;
    }
    return false;
}

void UiCoordinator::showLibrary() {
    if (libraryRuntime_ == nullptr) {
        return;
    }
    pendingNavigation_ = PendingNavigation::None;
    pendingIntent_ = PendingIntent::None;
    libraryRuntime_->library().openRoot();
    setPage(UiPage::Library);
}

void UiCoordinator::servicePendingIntent() {
    if (pendingIntent_ == PendingIntent::None ||
        pendingNavigation_ != PendingNavigation::None) {
        return;
    }
    const PendingIntent intent = pendingIntent_;
    pendingIntent_ = PendingIntent::None;
    const bool accepted = intent == PendingIntent::SelectLibrary
                              ? beginSelectedLibrary()
                              : activatePlaylistEntry();
    if (!accepted && externalError_[0] == '\0') {
        setExternalError("Pending action failed");
    }
}

void UiCoordinator::setHint(const char* hint) {
    const char* value = hint == nullptr ? "" : hint;
    if (std::strncmp(hint_, value, sizeof(hint_)) == 0) {
        return;
    }
    std::strncpy(hint_, value, sizeof(hint_) - 1);
    hint_[sizeof(hint_) - 1] = '\0';
    dirty_ = true;
}

void UiCoordinator::setExternalError(const char* error) {
    const char* value = error == nullptr ? "" : error;
    std::strncpy(externalError_, value, sizeof(externalError_) - 1);
    externalError_[sizeof(externalError_) - 1] = '\0';
    dirty_ = true;
}

void UiCoordinator::setGateCard(const char* text) {
    if(!text)text="";
    if(std::strcmp(gateCard_,text)==0)return;
    std::snprintf(gateCard_,sizeof(gateCard_),"%s",text);gateCardDirty_=true;dirty_=true;
    if(!gateCard_[0])nowPlaying_.invalidateDisplay();
}

UiPage UiCoordinator::page() const {
    return page_;
}

UiStats UiCoordinator::stats() const {
    return stats_;
}

bool UiCoordinator::currentTrackPath(char* output, size_t capacity) const {
    return player_ != nullptr && player_->currentPath(output, capacity);
}

void UiCoordinator::notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs) {
    // Receives the value AFTER the runtime applied it. Never simulates sound.
    nowPlaying_.notifyVolumeAdjusted(volume, nowMs);
}

void UiCoordinator::setPage(UiPage page) {
    if (page_ != page) {
        page_ = page;
        ++stats_.pageTransitions;
        nowPlaying_.setActive(page == UiPage::Player, millis());
        // A visible Player may have replaced the shared Metadata request.
        if (page == UiPage::Playlist) metadataRequested_ = false;
    }
    dirty_ = true;
}

void UiCoordinator::servicePendingNavigation() {
    MusicLibrary& library = libraryRuntime_->library();
    if (pendingNavigation_ == PendingNavigation::EnterLibrary) {
        if (library.state() == LibraryState::Ready &&
            std::strcmp(library.currentPath(), libraryRoot_) == 0) {
            pendingNavigation_ = PendingNavigation::None;
            playlistSelected_ = 0;
            setPath(lastPlaylistPath_, sizeof(lastPlaylistPath_),
                    library.currentPath());
            setPage(UiPage::Playlist);
        } else if (library.state() == LibraryState::Error) {
            pendingNavigation_ = PendingNavigation::None;
            setExternalError(libraryErrorName(library.error()));
        }
    } else if (pendingNavigation_ == PendingNavigation::SelectTrack) {
        if (!libraryRuntime_->selectionPending()) {
            char current[kTrackPathCapacity] = {};
            const PlayerSnapshot snapshot = player_->snapshot();
            if (player_->currentPath(current, sizeof(current)) &&
                std::strcmp(current, pendingTrackPath_) == 0 &&
                snapshot.state == PlayerState::Playing) {
                pendingNavigation_ = PendingNavigation::None;
                setPage(UiPage::Player);
            } else {
                pendingNavigation_ = PendingNavigation::None;
                setExternalError("Track selection failed");
            }
        }
    } else if (pendingNavigation_ == PendingNavigation::RestorePlaylist) {
        if (library.state() == LibraryState::Ready &&
            std::strcmp(library.currentPath(), lastPlaylistPath_) == 0) {
            pendingNavigation_ = PendingNavigation::None;
            playlistSelected_ = 0;
            setPage(UiPage::Playlist);
        } else if (library.state() == LibraryState::Error) {
            pendingNavigation_ = PendingNavigation::None;
            setExternalError(libraryErrorName(library.error()));
        }
    }
}

void UiCoordinator::serviceSelectedMetadata() {
    if (page_ != UiPage::Playlist || pendingNavigation_ != PendingNavigation::None) {
        return;
    }
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready || playlistCount() == 0) {
        return;
    }
    const size_t physical = physicalEntryIndex(playlistSelected_);
    LibraryEntry entry;
    if (library.entryAt(physical, entry) != LibraryResult::Ok ||
        entry.type != LibraryEntryType::Track) {
        selectedMetadataPath_[0] = '\0';
        selectedMetadataTitle_[0] = '\0';
        metadataRequested_ = false;
        return;
    }
    char path[kTrackPathCapacity] = {};
    if (library.entryPathAt(physical, path, sizeof(path)) != LibraryResult::Ok) {
        return;
    }
    if (std::strcmp(path, selectedMetadataPath_) != 0) {
        setPath(selectedMetadataPath_, sizeof(selectedMetadataPath_), path);
        selectedMetadataTitle_[0] = '\0';
        metadataRequested_ = false;
    }
    if (!metadataRequested_) {
        libraryRuntime_->requestMetadata(physical);
        metadataRequested_ = true;
        return;
    }

    const Mp3MetadataStatus status = libraryRuntime_->metadataStatus();
    if (status.state == Mp3MetadataState::Ready) {
        Mp3Metadata metadata;
        if (libraryRuntime_->metadataForPath(selectedMetadataPath_, metadata) &&
            metadata.title.value[0] != '\0' &&
            std::strcmp(selectedMetadataTitle_, metadata.title.value) != 0) {
            std::strncpy(selectedMetadataTitle_, metadata.title.value,
                         sizeof(selectedMetadataTitle_) - 1);
            dirty_ = true;
        }
    }
}

void UiCoordinator::render() {
    UiRenderContext& context = renderContext_;
    buildRenderContext(context);
    // Warm visible CJK only, before drawing. Retry later while the one-step
    // worker loads the glyph. ASCII-only historical screens remain immediate.
    if(!fonts_.requestUiWindow(context.libraryName,0,0,page_==UiPage::Library?194:123,1.5f)) {renderRetryRequested_=true;return;}
    if(page_==UiPage::Playlist)for(const auto& item:context.rows) if(item.valid && !fonts_.requestUiWindow(item.label,0,0,94,1.5f)) {renderRetryRequested_=true;return;}
    CachedUiFont font(&fonts_);display_->setFont(&font);
    const uint32_t started = micros();
    switch (page_) {
        case UiPage::Library:
            stats_.libraryText = LibraryPageRenderer::render(*display_, context);
            stats_.libraryTextIsBenchmark =
                std::strcmp(context.libraryName, "ADVWalkmanBenchmark") == 0;
            break;
        case UiPage::Playlist:
            PlaylistPageRenderer::render(*display_, context);
            break;
        case UiPage::Player:
            // Player is handled by the bounded NowPlayingPresenter path.
            break;
        case UiPage::Settings:
            SettingsPageRenderer::render(*display_, context);
            break;
    }
    const uint32_t elapsed = micros() - started;
    display_->setFont(&fonts::Font0);
    ++stats_.renderCount;
    stats_.renderMaxUs = std::max(stats_.renderMaxUs, elapsed);
}

void UiCoordinator::buildRenderContext(UiRenderContext& context) {
    context.page = page_;
    context.hint = hint_[0] == '\0' ? nullptr : hint_;
    context.error = externalError_[0] == '\0' ? nullptr : externalError_;
    setPath(context.libraryName, sizeof(context.libraryName), libraryName_);
    context.catalogIndex = catalog_.selectedIndex();
    context.catalogCount = catalog_.count();

    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() == LibraryState::Error && context.error == nullptr) {
        context.error = libraryErrorName(library.error());
    }
    if (page_ == UiPage::Library && library.state() == LibraryState::Ready &&
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0) {
        LibraryDescriptor descriptor;
        if (catalog_.selected(library, descriptor) == LibraryResult::Ok) {
            // descriptor is local: never leave a dangling pointer in the
            // context handed to the renderer after this function returns.
            setPath(context.libraryName, sizeof(context.libraryName),
                    descriptor.name);
        } else {
            renderRetryRequested_ = true;
        }
    }

    const PlayerSnapshot snapshot = player_->snapshot();
    context.playerState = playerStateName(snapshot.state);
    context.positionMs = snapshot.positionMs;
    context.currentTrack = lastCurrentTrack_;
    context.directoryPath = library.currentPath();
    context.playlistSelected = playlistSelected_;
    context.playlistCount = playlistCount();
    if (page_ == UiPage::Playlist) {
        buildPlaylistRows(context);
    }
}

void UiCoordinator::buildPlaylistRows(UiRenderContext& context) {
    for (auto& row : context.rows) {
        row.valid = false;
        row.playing = false;
    }
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready) {
        return;
    }
    const size_t count = playlistCount();
    const size_t windowStart = (playlistSelected_ / kP3AVisibleRows) *
                               kP3AVisibleRows;
    for (size_t rowIndex = 0; rowIndex < kP3AVisibleRows; ++rowIndex) {
        const size_t logical = windowStart + rowIndex;
        if (logical >= count) {
            break;
        }
        const size_t physical = physicalEntryIndex(logical);
        LibraryEntry entry;
        const LibraryResult result = library.entryAt(physical, entry);
        if (result == LibraryResult::Pending) {
            renderRetryRequested_ = true;
            continue;
        }
        if (result != LibraryResult::Ok) {
            continue;
        }
        PlaylistRenderRow& row = context.rows[rowIndex];
        row.valid = true;
        row.selected = logical == playlistSelected_;
        row.type = entry.type;
        trackLabel(entry.name, row.label, sizeof(row.label));

        char path[kTrackPathCapacity] = {};
        if (entry.type == LibraryEntryType::Track &&
            library.entryPathAt(physical, path, sizeof(path)) == LibraryResult::Ok) {
            row.playing = lastCurrentTrack_[0] != '\0' &&
                          std::strcmp(path, lastCurrentTrack_) == 0;
            if (row.selected && std::strcmp(path, selectedMetadataPath_) == 0 &&
                selectedMetadataTitle_[0] != '\0') {
                setPath(row.label, sizeof(row.label), selectedMetadataTitle_);
            }
        }
    }
}

bool UiCoordinator::beginSelectedLibrary() {
    MusicLibrary& library = libraryRuntime_->library();
    LibraryDescriptor descriptor;
    const LibraryResult selected = catalog_.selected(library, descriptor);
    if (selected == LibraryResult::Pending) {
        pendingIntent_ = PendingIntent::SelectLibrary;
        dirty_ = true;
        return true;
    }
    if (selected != LibraryResult::Ok) {
        setExternalError("Library unavailable");
        return false;
    }
    setPath(libraryName_, sizeof(libraryName_), descriptor.name);
    setPath(libraryRoot_, sizeof(libraryRoot_), descriptor.path);
    uncategorized_ = descriptor.uncategorized;
    playlistSelected_ = 0;
    selectedMetadataPath_[0] = '\0';
    selectedMetadataTitle_[0] = '\0';
    metadataRequested_ = false;

    const LibraryResult result = descriptor.uncategorized
                                     ? library.openRoot()
                                     : library.enter(descriptor.rootEntryIndex);
    if (result == LibraryResult::Error) {
        setExternalError(libraryErrorName(library.error()));
        return false;
    }
    if (result == LibraryResult::Ok) {
        setPath(lastPlaylistPath_, sizeof(lastPlaylistPath_),
                library.currentPath());
        setPage(UiPage::Playlist);
    } else {
        pendingNavigation_ = PendingNavigation::EnterLibrary;
        dirty_ = true;
    }
    return true;
}

bool UiCoordinator::activatePlaylistEntry() {
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready || playlistCount() == 0) {
        return false;
    }
    const size_t physical = physicalEntryIndex(playlistSelected_);
    LibraryEntry entry;
    const LibraryResult entryResult = library.entryAt(physical, entry);
    if (entryResult == LibraryResult::Pending) {
        pendingIntent_ = PendingIntent::ActivatePlaylistEntry;
        dirty_ = true;
        return true;
    }
    if (entryResult != LibraryResult::Ok) {
        setExternalError("Entry unavailable");
        return false;
    }
    if (entry.type == LibraryEntryType::Directory) {
        const LibraryResult result = library.enter(physical);
        if (result == LibraryResult::Error) {
            setExternalError(libraryErrorName(library.error()));
            return false;
        }
        playlistSelected_ = 0;
        selectedMetadataPath_[0] = '\0';
        metadataRequested_ = false;
        dirty_ = true;
        return true;
    }

    if (library.entryPathAt(physical, pendingTrackPath_,
                            sizeof(pendingTrackPath_)) != LibraryResult::Ok) {
        setExternalError("Track path unavailable");
        return false;
    }
    setPath(lastPlaylistPath_, sizeof(lastPlaylistPath_), library.currentPath());
    const LibraryResult result = libraryRuntime_->selectTrack(physical, true);
    if (result == LibraryResult::Error) {
        setExternalError("Track selection failed");
        return false;
    }
    if (result == LibraryResult::Ok) {
        setPage(UiPage::Player);
    } else {
        pendingNavigation_ = PendingNavigation::SelectTrack;
    }
    return true;
}

bool UiCoordinator::returnFromPlaylist() {
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready) {
        return false;
    }
    if (std::strcmp(library.currentPath(), libraryRoot_) == 0) {
        library.openRoot();
        setPage(UiPage::Library);
        return true;
    }
    const LibraryResult result = library.parent();
    if (result == LibraryResult::Error) {
        setExternalError(libraryErrorName(library.error()));
        return false;
    }
    playlistSelected_ = 0;
    selectedMetadataPath_[0] = '\0';
    metadataRequested_ = false;
    dirty_ = true;
    return true;
}

bool UiCoordinator::restorePlaylistForCurrentTrack() {
    // Repeated Esc while the directory is loading must not cancel/reopen the
    // same SD scan. Let the existing asynchronous navigation finish.
    if (pendingNavigation_ == PendingNavigation::RestorePlaylist) return true;
    char current[kTrackPathCapacity] = {};
    if (!player_->currentPath(current, sizeof(current))) {
        setExternalError("No current track");
        return false;
    }
    if (lastPlaylistPath_[0] == '\0' &&
        !parentDirectoryOfTrack(current, lastPlaylistPath_,
                                sizeof(lastPlaylistPath_))) {
        setExternalError("Invalid track path");
        return false;
    }
    if (!firstLevelLibraryOfTrack(current, libraryRoot_,
                                  sizeof(libraryRoot_), uncategorized_)) {
        setExternalError("Track outside /Music");
        return false;
    }
    displayNameFromPath(libraryRoot_, libraryName_, sizeof(libraryName_));
    const LibraryResult result =
        libraryRuntime_->library().openPath(lastPlaylistPath_);
    if (result == LibraryResult::Error) {
        setExternalError(libraryErrorName(libraryRuntime_->library().error()));
        return false;
    }
    if (result == LibraryResult::Ok) {
        playlistSelected_ = 0;
        setPage(UiPage::Playlist);
    } else {
        pendingNavigation_ = PendingNavigation::RestorePlaylist;
    }
    return true;
}

size_t UiCoordinator::playlistCount() const {
    if (libraryRuntime_ == nullptr) {
        return 0;
    }
    const MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready) {
        return 0;
    }
    return uncategorized_ &&
                   std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0
               ? library.trackCount()
               : library.entryCount();
}

size_t UiCoordinator::physicalEntryIndex(size_t logicalIndex) const {
    const MusicLibrary& library = libraryRuntime_->library();
    return uncategorized_ &&
                   std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0
               ? library.directoryCount() + logicalIndex
               : logicalIndex;
}

bool UiCoordinator::setPath(char* destination, size_t capacity,
                            const char* source) {
    if (destination == nullptr || capacity == 0 || source == nullptr) {
        return false;
    }
    const size_t length = std::strlen(source);
    if (length + 1 > capacity) {
        destination[0] = '\0';
        return false;
    }
    std::memcpy(destination, source, length + 1);
    return true;
}

bool UiCoordinator::parentDirectoryOfTrack(const char* track, char* output,
                                           size_t capacity) {
    if (track == nullptr || output == nullptr || capacity == 0) {
        return false;
    }
    const size_t rootLength = std::strlen(MusicLibrary::kMusicRoot);
    const size_t length = std::strlen(track);
    if (length <= rootLength + 1 || length + 1 > capacity ||
        std::strncmp(track, MusicLibrary::kMusicRoot, rootLength) != 0 ||
        track[rootLength] != '/' || std::strstr(track, "/../") != nullptr) {
        return false;
    }
    std::memcpy(output, track, length + 1);
    char* slash = std::strrchr(output, '/');
    if (slash == nullptr || slash < output + rootLength) {
        output[0] = '\0';
        return false;
    }
    *slash = '\0';
    return std::strlen(output) >= rootLength;
}

bool UiCoordinator::firstLevelLibraryOfTrack(const char* track, char* output,
                                             size_t capacity,
                                             bool& uncategorized) {
    char parent[kTrackPathCapacity] = {};
    if (!parentDirectoryOfTrack(track, parent, sizeof(parent))) {
        return false;
    }
    const size_t rootLength = std::strlen(MusicLibrary::kMusicRoot);
    if (std::strcmp(parent, MusicLibrary::kMusicRoot) == 0) {
        uncategorized = true;
        return std::snprintf(output, capacity, "%s", MusicLibrary::kMusicRoot) > 0;
    }
    const char* component = parent + rootLength + 1;
    const char* slash = std::strchr(component, '/');
    const size_t componentLength = slash == nullptr
                                       ? std::strlen(component)
                                       : static_cast<size_t>(slash - component);
    if (componentLength == 0 || rootLength + 1 + componentLength + 1 > capacity) {
        return false;
    }
    std::memcpy(output, MusicLibrary::kMusicRoot, rootLength);
    output[rootLength] = '/';
    std::memcpy(output + rootLength + 1, component, componentLength);
    output[rootLength + 1 + componentLength] = '\0';
    uncategorized = false;
    return true;
}

void UiCoordinator::displayNameFromPath(const char* path, char* output,
                                        size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    if (path == nullptr || std::strcmp(path, MusicLibrary::kMusicRoot) == 0) {
        std::snprintf(output, capacity, "%s", "Uncategorized");
        return;
    }
    const char* slash = std::strrchr(path, '/');
    std::snprintf(output, capacity, "%s", slash == nullptr ? path : slash + 1);
}

void UiCoordinator::trackLabel(const char* name, char* output,
                               size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    if (name == nullptr) {
        output[0] = '\0';
        return;
    }
    const size_t length = std::strlen(name);
    if (length >= capacity) {
        output[0] = '\0';
        return;
    }
    std::memcpy(output, name, length + 1);
    char* dot = std::strrchr(output, '.');
    if (dot != nullptr &&
        (std::strcmp(dot, ".mp3") == 0 || std::strcmp(dot, ".MP3") == 0)) {
        *dot = '\0';
    }
}

}  // namespace player
}  // namespace adv_walkman
