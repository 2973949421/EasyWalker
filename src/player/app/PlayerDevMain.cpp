#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>

#include <cstdlib>
#include <cstring>

#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"
#include "player/support/AdvStorage.h"

namespace {

using namespace adv_walkman::player;

constexpr size_t kCommandCapacity = 96;
constexpr uint32_t kStatusIntervalMs = 5000;

PlayerRuntime player;
LibraryRuntime libraryRuntime;
char command[kCommandCapacity] = {};
size_t commandLength = 0;
uint32_t lastStatusAtMs = 0;
LibraryState lastRenderedLibraryState = LibraryState::Idle;
PlayerState lastRenderedPlayerState = PlayerState::Empty;
char lastRenderedPath[kTrackPathCapacity] = {};

enum class SerialOutputMode : uint8_t { None, LibraryEntries, RecentTracks };
SerialOutputMode serialOutputMode = SerialOutputMode::None;
size_t serialOutputIndex = 0;

void printPlayerStatus() {
    const PlayerSnapshot snapshot = player.snapshot();
    char path[kTrackPathCapacity] = {};
    player.currentPath(path, sizeof(path));
    Serial.printf(
        "player state=%s error=%s audio_error=%s track=%u/%u "
        "position_ms=%lu sample_rate=%lu repeat=%s shuffle=%s "
        "backpressure=%lu service_max_us=%lu path=%s\n",
        playerStateName(snapshot.state), playerErrorName(snapshot.error),
        audioErrorName(snapshot.audioError),
        static_cast<unsigned>(snapshot.hasCurrent ? snapshot.currentIndex + 1 : 0),
        static_cast<unsigned>(snapshot.queueCount),
        static_cast<unsigned long>(snapshot.positionMs),
        static_cast<unsigned long>(snapshot.sampleRateHz),
        repeatModeName(snapshot.repeatMode),
        snapshot.shuffleEnabled ? "on" : "off",
        static_cast<unsigned long>(snapshot.backpressureEvents),
        static_cast<unsigned long>(snapshot.serviceMaxUs),
        path[0] == '\0' ? "none" : path);
}

void printLibraryStatus() {
    const MusicLibrary& library = libraryRuntime.library();
    const LibraryStats stats = library.stats();
    Serial.printf(
        "library state=%s error=%s path=%s entries=%u dirs=%u tracks=%u "
        "cache_hit=%lu cache_miss=%lu eviction=%lu page_hit=%lu "
        "page_miss=%lu service_max_us=%lu entry_read_max_us=%lu "
        "metadata_cache=%u recent=%u recent_result=%s\n",
        libraryStateName(library.state()), libraryErrorName(library.error()),
        library.currentPath(), static_cast<unsigned>(library.entryCount()),
        static_cast<unsigned>(library.directoryCount()),
        static_cast<unsigned>(library.trackCount()),
        static_cast<unsigned long>(stats.cacheHits),
        static_cast<unsigned long>(stats.cacheMisses),
        static_cast<unsigned long>(stats.cacheEvictions),
        static_cast<unsigned long>(stats.pageHits),
        static_cast<unsigned long>(stats.pageMisses),
        static_cast<unsigned long>(stats.serviceMaxUs),
        static_cast<unsigned long>(stats.entryReadMaxUs),
        static_cast<unsigned>(libraryRuntime.metadataCacheSize()),
        static_cast<unsigned>(libraryRuntime.recentCount()),
        recentTracksResultName(libraryRuntime.recentResult()));
}

bool parseIndex(const char* input, size_t& output) {
    if (input == nullptr || *input == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(input, &end, 10);
    if (end == input || *end != '\0' || value > SIZE_MAX) {
        return false;
    }
    output = static_cast<size_t>(value);
    return true;
}

void startEntryOutput() {
    serialOutputMode = SerialOutputMode::LibraryEntries;
    serialOutputIndex = 0;
    Serial.println("lib ls started=1");
}

void serviceEntryOutput() {
    MusicLibrary& library = libraryRuntime.library();
    if (library.state() != LibraryState::Ready) {
        if (library.state() == LibraryState::Error) {
            Serial.printf("lib ls error index=%u error=%s\n",
                          static_cast<unsigned>(serialOutputIndex),
                          libraryErrorName(library.error()));
            serialOutputMode = SerialOutputMode::None;
        }
        return;
    }
    if (serialOutputIndex >= library.entryCount()) {
        Serial.printf("lib ls complete=1 count=%u\n",
                      static_cast<unsigned>(library.entryCount()));
        serialOutputMode = SerialOutputMode::None;
        return;
    }
    LibraryEntry entry;
    const LibraryResult result = library.entryAt(serialOutputIndex, entry);
    if (result == LibraryResult::Pending) {
        return;
    }
    if (result != LibraryResult::Ok) {
        Serial.printf("lib ls error index=%u error=%s\n",
                      static_cast<unsigned>(serialOutputIndex),
                      libraryErrorName(library.error()));
        serialOutputMode = SerialOutputMode::None;
        return;
    }
    Serial.printf("lib entry index=%u type=%s name=%s\n",
                  static_cast<unsigned>(serialOutputIndex),
                  entry.type == LibraryEntryType::Directory ? "dir" : "track",
                  entry.name);
    ++serialOutputIndex;
}

void startRecentOutput() {
    serialOutputMode = SerialOutputMode::RecentTracks;
    serialOutputIndex = 0;
    Serial.println("recent started=1");
}

void serviceRecentOutput() {
    if (serialOutputIndex >= libraryRuntime.recentCount()) {
        Serial.printf("recent complete=1 count=%u\n",
                      static_cast<unsigned>(libraryRuntime.recentCount()));
        serialOutputMode = SerialOutputMode::None;
        return;
    }
    char path[kTrackPathCapacity] = {};
    if (!libraryRuntime.recentPathAt(serialOutputIndex, path, sizeof(path))) {
        Serial.printf("recent error index=%u\n",
                      static_cast<unsigned>(serialOutputIndex));
        serialOutputMode = SerialOutputMode::None;
        return;
    }
    Serial.printf("recent index=%u path=%s\n",
                  static_cast<unsigned>(serialOutputIndex), path);
    ++serialOutputIndex;
}

void serviceSerialOutput() {
    if (serialOutputMode == SerialOutputMode::LibraryEntries) {
        serviceEntryOutput();
    } else if (serialOutputMode == SerialOutputMode::RecentTracks) {
        serviceRecentOutput();
    }
}

void printMetadata() {
    Mp3Metadata metadata;
    const Mp3MetadataStatus status = libraryRuntime.metadataStatus();
    if (!libraryRuntime.metadata(metadata)) {
        Serial.printf("metadata ready=0 state=%s error=%s\n",
                      mp3MetadataStateName(status.state),
                      mp3MetadataErrorName(status.error));
        return;
    }
    Serial.printf(
        "metadata ready=1 warning=%s title=%s artist=%s album=%s track=%s "
        "title_fallback=%d truncated=%d\n",
        mp3MetadataErrorName(status.error), metadata.title.value,
        metadata.artist.value, metadata.album.value,
        metadata.trackNumber.value, metadata.titleFromFilename,
        metadata.title.truncated || metadata.artist.truncated ||
            metadata.album.truncated || metadata.trackNumber.truncated);
}

void executeCommand(char* input) {
    while (*input == ' ') {
        ++input;
    }
    MusicLibrary& library = libraryRuntime.library();
    size_t index = 0;
    serialOutputMode = SerialOutputMode::None;
    if (std::strcmp(input, "status") == 0) {
        printPlayerStatus();
        printLibraryStatus();
    } else if (std::strcmp(input, "play") == 0) {
        Serial.printf("command play ok=%d\n", player.play());
    } else if (std::strcmp(input, "pause") == 0) {
        Serial.printf("command pause ok=%d\n", player.pause());
    } else if (std::strcmp(input, "resume") == 0) {
        Serial.printf("command resume ok=%d\n", player.resume());
    } else if (std::strcmp(input, "stop") == 0) {
        player.stop();
        Serial.println("command stop ok=1");
    } else if (std::strcmp(input, "next") == 0) {
        Serial.printf("command next ok=%d\n", player.next());
    } else if (std::strcmp(input, "prev") == 0) {
        Serial.printf("command prev ok=%d\n", player.previous());
    } else if (std::strcmp(input, "lib root") == 0) {
        Serial.printf("lib root result=%u\n",
                      static_cast<unsigned>(library.openRoot()));
    } else if (std::strcmp(input, "lib up") == 0) {
        Serial.printf("lib up result=%u\n",
                      static_cast<unsigned>(library.parent()));
    } else if (std::strcmp(input, "lib refresh") == 0) {
        Serial.printf("lib refresh result=%u\n",
                      static_cast<unsigned>(library.refreshCurrent()));
    } else if (std::strcmp(input, "lib ls") == 0) {
        startEntryOutput();
    } else if (std::strncmp(input, "lib cd ", 7) == 0 &&
               parseIndex(input + 7, index)) {
        Serial.printf("lib cd result=%u index=%u\n",
                      static_cast<unsigned>(library.enter(index)),
                      static_cast<unsigned>(index));
    } else if (std::strncmp(input, "lib play ", 9) == 0 &&
               parseIndex(input + 9, index)) {
        Serial.printf("lib play result=%u index=%u\n",
                      static_cast<unsigned>(libraryRuntime.selectTrack(index)),
                      static_cast<unsigned>(index));
    } else if (std::strncmp(input, "lib meta ", 9) == 0 &&
               parseIndex(input + 9, index)) {
        Serial.printf("lib meta result=%u index=%u\n",
                      static_cast<unsigned>(libraryRuntime.requestMetadata(index)),
                      static_cast<unsigned>(index));
    } else if (std::strcmp(input, "lib meta") == 0) {
        printMetadata();
    } else if (std::strcmp(input, "lib recent") == 0) {
        startRecentOutput();
    } else {
        Serial.println(
            "commands: status play pause resume stop next prev | "
            "lib root|up|refresh|ls|cd <index>|play <index>|meta <index>|meta|recent");
    }
}

void serviceSerial() {
    // Consume a bounded number of input bytes and at most one command per
    // loop so USB terminal traffic cannot delay audio service indefinitely.
    size_t consumed = 0;
    while (Serial.available() > 0 && consumed < 24) {
        const char value = static_cast<char>(Serial.read());
        ++consumed;
        if (value == '\r' || value == '\n') {
            if (commandLength > 0) {
                command[commandLength] = '\0';
                executeCommand(command);
                commandLength = 0;
                return;
            }
        } else if (commandLength + 1 < sizeof(command)) {
            command[commandLength++] = value;
        }
    }
}

void renderIfChanged() {
    const MusicLibrary& library = libraryRuntime.library();
    const PlayerSnapshot snapshot = player.snapshot();
    const bool pathChanged =
        std::strcmp(lastRenderedPath, library.currentPath()) != 0;
    if (!pathChanged && snapshot.state == lastRenderedPlayerState &&
        library.state() == lastRenderedLibraryState) {
        return;
    }
    std::strncpy(lastRenderedPath, library.currentPath(),
                 sizeof(lastRenderedPath) - 1);
    lastRenderedPlayerState = snapshot.state;
    lastRenderedLibraryState = library.state();

    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(6, 6);
    display.setTextColor(GREEN, BLACK);
    display.setTextSize(1.3f);
    display.println("ADV Walkman P2 Dev");
    display.setTextSize(1.0f);
    display.setTextColor(WHITE, BLACK);
    display.printf("Player: %s\n", playerStateName(snapshot.state));
    display.printf("Library: %s\n", libraryStateName(library.state()));
    display.printf("Entries: %u  Tracks: %u\n",
                   static_cast<unsigned>(library.entryCount()),
                   static_cast<unsigned>(library.trackCount()));
    display.printf("Path: %.34s\n", library.currentPath());
    if (library.state() == LibraryState::Error) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("Error: %s\n", libraryErrorName(library.error()));
    }
    display.setTextColor(YELLOW, BLACK);
    display.println("Serial: lib ... / status");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t waitStarted = millis();
    while (!Serial && millis() - waitStarted < 1500) {
        delay(10);
    }
    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);

    const bool sdReady = mountAdvSd();
    const bool playerReady = sdReady && player.begin(true);
    const bool libraryReady = playerReady && libraryRuntime.begin(SD, player);
    Serial.printf("boot app=adv-walkman version=%s sd=%d player=%d library=%d\n",
                  ADV_WALKMAN_VERSION, sdReady, playerReady, libraryReady);
    if (libraryReady) {
        libraryRuntime.library().openRoot();
    }
    printPlayerStatus();
    printLibraryStatus();
    renderIfChanged();
    lastStatusAtMs = millis();
}

void loop() {
    M5Cardputer.update();
    player.service();
    libraryRuntime.service();
    serviceSerial();
    serviceSerialOutput();
    renderIfChanged();
    const uint32_t now = millis();
    if (now - lastStatusAtMs >= kStatusIntervalMs) {
        lastStatusAtMs = now;
        printPlayerStatus();
        printLibraryStatus();
    }
    delay(1);
}
