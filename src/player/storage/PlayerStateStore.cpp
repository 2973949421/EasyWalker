#include "PlayerStateStore.h"

#include <algorithm>
#include <string.h>

namespace adv_walkman {
namespace player {

namespace {

constexpr uint32_t kQueueMagic = 0x31515741;    // "AWQ1" on disk.
constexpr uint32_t kSessionMagic = 0x31535741;  // "AWS1" on disk.
constexpr uint16_t kFormatVersion = 1;
constexpr uint16_t kRecordHeaderSize = 20;
constexpr size_t kStorageStepBytes = 1024;
constexpr uint32_t kMaximumSessionPayload =
    24 + (kPersistedQueueMaxTracks * sizeof(uint16_t)) +
    (kPersistedHistoryMaxTracks * sizeof(uint16_t));
static_assert(kMaximumSessionPayload <= 4096, "session must fit one write chunk");

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t updateCrc32(uint32_t state, const uint8_t* data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        state ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = -(state & 1U);
            state = (state >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return state;
}

bool generationIsNewer(uint32_t candidate, uint32_t reference) {
    return static_cast<int32_t>(candidate - reference) > 0;
}

uint32_t nextGeneration(uint32_t current) {
    const uint32_t next = current + 1;
    return next == 0 ? 1 : next;
}

bool validUtf8Path(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return false;
    }

    size_t index = 0;
    while (index < length) {
        const uint8_t first = data[index++];
        if (first == 0) {
            return false;
        }
        if (first <= 0x7F) {
            continue;
        }

        size_t trailing = 0;
        uint8_t secondMinimum = 0x80;
        uint8_t secondMaximum = 0xBF;
        if (first >= 0xC2 && first <= 0xDF) {
            trailing = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            trailing = 2;
            if (first == 0xE0) {
                secondMinimum = 0xA0;
            } else if (first == 0xED) {
                secondMaximum = 0x9F;
            }
        } else if (first >= 0xF0 && first <= 0xF4) {
            trailing = 3;
            if (first == 0xF0) {
                secondMinimum = 0x90;
            } else if (first == 0xF4) {
                secondMaximum = 0x8F;
            }
        } else {
            return false;
        }

        if (index + trailing > length || data[index] < secondMinimum ||
            data[index] > secondMaximum) {
            return false;
        }
        ++index;
        for (size_t remaining = 1; remaining < trailing; ++remaining, ++index) {
            if ((data[index] & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

bool encodeSession(
    const PersistedSession& session,
    uint8_t* output,
    uint32_t capacity,
    uint32_t& outputLength) {
    if (session.repeatMode > 2 ||
        (session.currentIndex != kPersistedInvalidTrackIndex &&
         session.currentIndex >= kPersistedQueueMaxTracks) ||
        session.orderCount > kPersistedQueueMaxTracks ||
        session.orderCursor > session.orderCount ||
        session.historyCount > kPersistedHistoryMaxTracks) {
        return false;
    }

    const uint32_t required =
        24 + (static_cast<uint32_t>(session.orderCount) * 2) +
        (static_cast<uint32_t>(session.historyCount) * 2);
    if (required > capacity) {
        return false;
    }

    for (uint16_t index = 0; index < session.orderCount; ++index) {
        if (session.order[index] >= kPersistedQueueMaxTracks) {
            return false;
        }
    }
    for (uint8_t index = 0; index < session.historyCount; ++index) {
        if (session.history[index] >= kPersistedQueueMaxTracks) {
            return false;
        }
    }

    memset(output, 0, required);
    writeLe32(output + 0, session.queueGeneration);
    writeLe16(output + 4, session.currentIndex);
    output[6] = session.repeatMode;
    output[7] = session.shuffleEnabled ? 1 : 0;
    writeLe32(output + 8, session.positionMs);
    writeLe32(output + 12, session.sourceOffset);
    writeLe16(output + 16, session.orderCount);
    writeLe16(output + 18, session.orderCursor);
    output[20] = session.historyCount;
    output[21] = session.preferredNowPlayingView == 1 ? 1 : 0;

    uint32_t offset = 24;
    for (uint16_t index = 0; index < session.orderCount; ++index) {
        writeLe16(output + offset, session.order[index]);
        offset += 2;
    }
    for (uint8_t index = 0; index < session.historyCount; ++index) {
        writeLe16(output + offset, session.history[index]);
        offset += 2;
    }
    outputLength = required;
    return true;
}

bool decodeSession(
    const uint8_t* input,
    uint32_t length,
    PersistedSession& output) {
    if (input == nullptr || length < 24) {
        return false;
    }

    memset(&output, 0, sizeof(output));
    output.currentIndex = kPersistedInvalidTrackIndex;
    output.queueGeneration = readLe32(input + 0);
    output.currentIndex = readLe16(input + 4);
    output.repeatMode = input[6];
    output.shuffleEnabled = input[7] != 0;
    output.positionMs = readLe32(input + 8);
    output.sourceOffset = readLe32(input + 12);
    output.orderCount = readLe16(input + 16);
    output.orderCursor = readLe16(input + 18);
    output.historyCount = input[20];
    output.preferredNowPlayingView = input[21] == 1 ? 1 : 0;

    const uint32_t expected =
        24 + (static_cast<uint32_t>(output.orderCount) * 2) +
        (static_cast<uint32_t>(output.historyCount) * 2);
    if (output.repeatMode > 2 ||
        (output.currentIndex != kPersistedInvalidTrackIndex &&
         output.currentIndex >= kPersistedQueueMaxTracks) ||
        output.orderCount > kPersistedQueueMaxTracks ||
        output.orderCursor > output.orderCount ||
        output.historyCount > kPersistedHistoryMaxTracks ||
        expected != length) {
        memset(&output, 0, sizeof(output));
        output.currentIndex = kPersistedInvalidTrackIndex;
        return false;
    }

    uint32_t offset = 24;
    for (uint16_t index = 0; index < output.orderCount; ++index) {
        output.order[index] = readLe16(input + offset);
        if (output.order[index] >= kPersistedQueueMaxTracks) {
            memset(&output, 0, sizeof(output));
            output.currentIndex = kPersistedInvalidTrackIndex;
            return false;
        }
        offset += 2;
    }
    for (uint8_t index = 0; index < output.historyCount; ++index) {
        output.history[index] = readLe16(input + offset);
        if (output.history[index] >= kPersistedQueueMaxTracks) {
            memset(&output, 0, sizeof(output));
            output.currentIndex = kPersistedInvalidTrackIndex;
            return false;
        }
        offset += 2;
    }
    return true;
}

}  // namespace

const char* const PlayerStateStore::kStateDirectory = "/ADVWalkman/state";
const char* const PlayerStateStore::kQueueSlotA =
    "/ADVWalkman/state/queue-a.bin";
const char* const PlayerStateStore::kQueueSlotB =
    "/ADVWalkman/state/queue-b.bin";
const char* const PlayerStateStore::kSessionSlotA =
    "/ADVWalkman/state/session-a.bin";
const char* const PlayerStateStore::kSessionSlotB =
    "/ADVWalkman/state/session-b.bin";

const char* persistenceResultName(PersistenceResult result) {
    switch (result) {
        case PersistenceResult::Ok:
            return "OK";
        case PersistenceResult::Pending:
            return "PENDING";
        case PersistenceResult::NotFound:
            return "NOT_FOUND";
        case PersistenceResult::Busy:
            return "BUSY";
        case PersistenceResult::InvalidArgument:
            return "INVALID_ARGUMENT";
        case PersistenceResult::UnsupportedVersion:
            return "UNSUPPORTED_VERSION";
        case PersistenceResult::Corrupt:
            return "CORRUPT";
        case PersistenceResult::IoError:
            return "IO_ERROR";
        default:
            return "UNKNOWN";
    }
}

bool PlayerStateStore::begin(fs::FS& fs) {
    fs_ = &fs;
    if (!ensureStateDirectory()) {
        lastResult_ = PersistenceResult::IoError;
        return false;
    }
    refreshQueueSlots();
    refreshSessionSlots();
    lastResult_ = PersistenceResult::Ok;
    return true;
}

PersistenceResult PlayerStateStore::loadQueue(PersistedQueueSource& output) {
    output.clear();
    if (pending()) {
        return PersistenceResult::Busy;
    }
    if (fs_ == nullptr) {
        return lastResult_ = PersistenceResult::IoError;
    }
    refreshQueueSlots();
    if (!currentQueueSlot_.valid) {
        return lastResult_ = PersistenceResult::NotFound;
    }
    if (!output.attach(*fs_, currentQueueSlot_.path, currentQueueSlot_.generation)) {
        return lastResult_ = PersistenceResult::Corrupt;
    }
    return lastResult_ = PersistenceResult::Ok;
}

PersistenceResult PlayerStateStore::loadSession(PersistedSession& output) {
    memset(&output, 0, sizeof(output));
    output.currentIndex = kPersistedInvalidTrackIndex;
    if (pending()) {
        return PersistenceResult::Busy;
    }
    if (fs_ == nullptr) {
        return lastResult_ = PersistenceResult::IoError;
    }
    refreshSessionSlots();
    if (!currentSessionSlot_.valid) {
        return lastResult_ = PersistenceResult::NotFound;
    }
    if (!validateSessionPayload(currentSessionSlot_, &output)) {
        memset(&output, 0, sizeof(output));
        output.currentIndex = kPersistedInvalidTrackIndex;
        return lastResult_ = PersistenceResult::Corrupt;
    }
    return lastResult_ = PersistenceResult::Ok;
}

PersistenceResult PlayerStateStore::loadPairedState(
    PersistedQueueSource& queueOutput,
    PersistedSession& sessionOutput) {
    queueOutput.clear();
    memset(&sessionOutput, 0, sizeof(sessionOutput));
    sessionOutput.currentIndex = kPersistedInvalidTrackIndex;
    if (pending()) {
        return PersistenceResult::Busy;
    }
    if (fs_ == nullptr) {
        return lastResult_ = PersistenceResult::IoError;
    }

    // Keep the newest-slot cursors current for the next alternating write,
    // but restore from the newest Session whose referenced Queue generation
    // is still present. This survives power loss between Queue and Session
    // publication without discarding the older complete pair.
    refreshQueueSlots();
    refreshSessionSlots();

    SlotInfo queueSlots[2];
    queueSlots[0].path = kQueueSlotA;
    queueSlots[0].slot = 'A';
    queueSlots[1].path = kQueueSlotB;
    queueSlots[1].slot = 'B';
    for (size_t index = 0; index < 2; ++index) {
        queueSlots[index].valid = inspectRecord(
            queueSlots[index].path, kQueueMagic,
            kPersistedQueueMaxPayloadBytes, queueSlots[index]) &&
            validateQueuePayload(queueSlots[index]);
    }

    SlotInfo sessionSlots[2];
    sessionSlots[0].path = kSessionSlotA;
    sessionSlots[0].slot = 'A';
    sessionSlots[1].path = kSessionSlotB;
    sessionSlots[1].slot = 'B';
    for (size_t index = 0; index < 2; ++index) {
        sessionSlots[index].valid = inspectRecord(
            sessionSlots[index].path, kSessionMagic,
            kMaximumSessionPayload, sessionSlots[index]) &&
            validateSessionPayload(sessionSlots[index], nullptr);
    }
    if (sessionSlots[1].valid &&
        (!sessionSlots[0].valid ||
         generationIsNewer(sessionSlots[1].generation,
                           sessionSlots[0].generation))) {
        const SlotInfo temporary = sessionSlots[0];
        sessionSlots[0] = sessionSlots[1];
        sessionSlots[1] = temporary;
    }

    const bool sawAnyRecord =
        fs_->exists(kQueueSlotA) || fs_->exists(kQueueSlotB) ||
        fs_->exists(kSessionSlotA) || fs_->exists(kSessionSlotB);
    bool sawValidRecord = false;
    for (size_t sessionIndex = 0; sessionIndex < 2; ++sessionIndex) {
        if (!sessionSlots[sessionIndex].valid) {
            continue;
        }
        sawValidRecord = true;
        if (!validateSessionPayload(sessionSlots[sessionIndex],
                                    &sessionOutput)) {
            continue;
        }
        for (size_t queueIndex = 0; queueIndex < 2; ++queueIndex) {
            if (!queueSlots[queueIndex].valid) {
                continue;
            }
            sawValidRecord = true;
            if (queueSlots[queueIndex].generation !=
                sessionOutput.queueGeneration) {
                continue;
            }
            if (queueOutput.attach(*fs_, queueSlots[queueIndex].path,
                                   queueSlots[queueIndex].generation)) {
                // Anchor the next alternating writes to the recovered pair,
                // not to a newer orphan record. The opposite slots can then
                // be overwritten while this complete Queue+Session remains
                // recoverable through another interrupted publication.
                currentQueueSlot_ = queueSlots[queueIndex];
                currentSessionSlot_ = sessionSlots[sessionIndex];
                return lastResult_ = PersistenceResult::Ok;
            }
        }
    }

    queueOutput.clear();
    memset(&sessionOutput, 0, sizeof(sessionOutput));
    sessionOutput.currentIndex = kPersistedInvalidTrackIndex;
    return lastResult_ = (sawValidRecord || sawAnyRecord)
                             ? PersistenceResult::Corrupt
                             : PersistenceResult::NotFound;
}

PersistenceResult PlayerStateStore::saveQueueAsync(const TrackSource& source) {
    if (pending()) {
        return PersistenceResult::Busy;
    }
    if (fs_ == nullptr || source.count() > kPersistedQueueMaxTracks) {
        return lastResult_ = PersistenceResult::InvalidArgument;
    }

    queueSource_ = &source;
    queueCount_ = static_cast<uint16_t>(source.count());
    queuePrepareIndex_ = 0;
    queueWriteIndex_ = 0;
    queueCountWritten_ = false;
    payloadWriteOffset_ = 0;
    preparedPayloadLength_ = sizeof(uint16_t);
    preparedPayloadCrcState_ = 0xFFFFFFFF;
    uint8_t countBytes[2];
    writeLe16(countBytes, queueCount_);
    preparedPayloadCrcState_ = updateCrc32(
        preparedPayloadCrcState_, countBytes, sizeof(countBytes));

    jobKind_ = PersistenceRecordKind::Queue;
    completedKind_ = PersistenceRecordKind::None;
    jobGeneration_ = nextGeneration(currentQueueSlot_.generation);
    if (currentQueueSlot_.valid && currentQueueSlot_.slot == 'A') {
        targetPath_ = kQueueSlotB;
        targetSlot_ = 'B';
    } else {
        targetPath_ = kQueueSlotA;
        targetSlot_ = 'A';
    }
    phase_ = JobPhase::QueuePrepare;
    return lastResult_ = PersistenceResult::Pending;
}

PersistenceResult PlayerStateStore::saveSessionAsync(
    const PersistedSession& session) {
    if (pending()) {
        return PersistenceResult::Busy;
    }
    if (fs_ == nullptr || !encodeSession(
            session,
            ioBuffer_,
            sizeof(ioBuffer_),
            sessionPayloadLength_)) {
        return lastResult_ = PersistenceResult::InvalidArgument;
    }

    preparedPayloadLength_ = sessionPayloadLength_;
    preparedPayloadCrcState_ = updateCrc32(
        0xFFFFFFFF, ioBuffer_, sessionPayloadLength_);
    preparedPayloadCrc_ = preparedPayloadCrcState_ ^ 0xFFFFFFFF;
    payloadWriteOffset_ = 0;

    jobKind_ = PersistenceRecordKind::Session;
    completedKind_ = PersistenceRecordKind::None;
    jobGeneration_ = nextGeneration(currentSessionSlot_.generation);
    if (currentSessionSlot_.valid && currentSessionSlot_.slot == 'A') {
        targetPath_ = kSessionSlotB;
        targetSlot_ = 'B';
    } else {
        targetPath_ = kSessionSlotA;
        targetSlot_ = 'A';
    }
    phase_ = JobPhase::OpenTarget;
    return lastResult_ = PersistenceResult::Pending;
}

void PlayerStateStore::service() {
    switch (phase_) {
        case JobPhase::Idle:
            return;
        case JobPhase::QueuePrepare:
            serviceQueuePrepare();
            return;
        case JobPhase::OpenTarget:
            serviceOpenTarget();
            return;
        case JobPhase::WriteHeader:
            serviceWriteHeader();
            return;
        case JobPhase::WritePayload:
            serviceWritePayload();
            return;
        case JobPhase::CloseTarget:
            serviceCloseTarget();
            return;
        case JobPhase::OpenVerify:
            serviceOpenVerify();
            return;
        case JobPhase::VerifyPayload:
            serviceVerifyPayload();
            return;
    }
}

bool PlayerStateStore::pending() const {
    return phase_ != JobPhase::Idle;
}

PersistenceResult PlayerStateStore::lastResult() const {
    return lastResult_;
}

PersistenceRecordKind PlayerStateStore::lastCompletedKind() const {
    return completedKind_;
}

uint32_t PlayerStateStore::latestQueueGeneration() const {
    return currentQueueSlot_.generation;
}

uint32_t PlayerStateStore::latestSessionGeneration() const {
    return currentSessionSlot_.generation;
}

bool PlayerStateStore::ensureStateDirectory() {
    if (fs_ == nullptr) {
        return false;
    }
    if (!fs_->exists("/ADVWalkman") && !fs_->mkdir("/ADVWalkman")) {
        return false;
    }
    return fs_->exists(kStateDirectory) || fs_->mkdir(kStateDirectory);
}

void PlayerStateStore::refreshQueueSlots() {
    SlotInfo slotA;
    SlotInfo slotB;
    slotA.path = kQueueSlotA;
    slotA.slot = 'A';
    slotB.path = kQueueSlotB;
    slotB.slot = 'B';
    slotA.valid = inspectRecord(
        kQueueSlotA, kQueueMagic, kPersistedQueueMaxPayloadBytes, slotA) &&
        validateQueuePayload(slotA);
    slotB.valid = inspectRecord(
        kQueueSlotB, kQueueMagic, kPersistedQueueMaxPayloadBytes, slotB) &&
        validateQueuePayload(slotB);

    if (slotA.valid &&
        (!slotB.valid || generationIsNewer(slotA.generation, slotB.generation))) {
        currentQueueSlot_ = slotA;
    } else if (slotB.valid) {
        currentQueueSlot_ = slotB;
    } else {
        currentQueueSlot_ = SlotInfo{};
    }
}

void PlayerStateStore::refreshSessionSlots() {
    SlotInfo slotA;
    SlotInfo slotB;
    slotA.path = kSessionSlotA;
    slotA.slot = 'A';
    slotB.path = kSessionSlotB;
    slotB.slot = 'B';
    slotA.valid = inspectRecord(
        kSessionSlotA, kSessionMagic, kMaximumSessionPayload, slotA) &&
        validateSessionPayload(slotA, nullptr);
    slotB.valid = inspectRecord(
        kSessionSlotB, kSessionMagic, kMaximumSessionPayload, slotB) &&
        validateSessionPayload(slotB, nullptr);

    if (slotA.valid &&
        (!slotB.valid || generationIsNewer(slotA.generation, slotB.generation))) {
        currentSessionSlot_ = slotA;
    } else if (slotB.valid) {
        currentSessionSlot_ = slotB;
    } else {
        currentSessionSlot_ = SlotInfo{};
    }
}

bool PlayerStateStore::inspectRecord(
    const char* path,
    uint32_t expectedMagic,
    uint32_t maximumPayload,
    SlotInfo& output) const {
    if (fs_ == nullptr || path == nullptr || !fs_->exists(path)) {
        return false;
    }

    fs::File file = fs_->open(path, FILE_READ);
    uint8_t header[kRecordHeaderSize] = {};
    if (!file || file.read(header, sizeof(header)) != sizeof(header)) {
        if (file) {
            file.close();
        }
        return false;
    }

    const uint32_t magic = readLe32(header + 0);
    const uint16_t version = readLe16(header + 4);
    const uint16_t headerSize = readLe16(header + 6);
    const uint32_t generation = readLe32(header + 8);
    const uint32_t payloadLength = readLe32(header + 12);
    const uint32_t payloadCrc = readLe32(header + 16);
    if (magic != expectedMagic || version != kFormatVersion ||
        headerSize != kRecordHeaderSize || generation == 0 ||
        payloadLength > maximumPayload ||
        file.size() != kRecordHeaderSize + payloadLength) {
        file.close();
        return false;
    }

    uint32_t remaining = payloadLength;
    uint32_t crcState = 0xFFFFFFFF;
    uint8_t buffer[512];
    while (remaining > 0) {
        const size_t request = std::min<size_t>(remaining, sizeof(buffer));
        const size_t bytesRead = file.read(buffer, request);
        if (bytesRead != request) {
            file.close();
            return false;
        }
        crcState = updateCrc32(crcState, buffer, bytesRead);
        remaining -= bytesRead;
    }
    file.close();
    if ((crcState ^ 0xFFFFFFFF) != payloadCrc) {
        return false;
    }

    output.valid = true;
    output.generation = generation;
    output.payloadLength = payloadLength;
    output.payloadCrc = payloadCrc;
    output.path = path;
    return true;
}

bool PlayerStateStore::validateQueuePayload(const SlotInfo& slot) const {
    if (!slot.valid || slot.payloadLength < sizeof(uint16_t)) {
        return false;
    }
    fs::File file = fs_->open(slot.path, FILE_READ);
    if (!file || !file.seek(kRecordHeaderSize)) {
        if (file) {
            file.close();
        }
        return false;
    }

    uint8_t pair[2];
    if (file.read(pair, sizeof(pair)) != sizeof(pair)) {
        file.close();
        return false;
    }
    const uint16_t count = readLe16(pair);
    if (count > kPersistedQueueMaxTracks) {
        file.close();
        return false;
    }

    uint32_t consumed = sizeof(uint16_t);
    uint8_t path[kPersistedPathMaxBytes];
    for (uint16_t index = 0; index < count; ++index) {
        if (file.read(pair, sizeof(pair)) != sizeof(pair)) {
            file.close();
            return false;
        }
        const uint16_t pathLength = readLe16(pair);
        consumed += sizeof(uint16_t);
        if (pathLength == 0 || pathLength > sizeof(path) ||
            consumed + pathLength > slot.payloadLength ||
            file.read(path, pathLength) != pathLength ||
            !validUtf8Path(path, pathLength)) {
            file.close();
            return false;
        }
        consumed += pathLength;
    }
    file.close();
    return consumed == slot.payloadLength;
}

bool PlayerStateStore::validateSessionPayload(
    const SlotInfo& slot,
    PersistedSession* output) {
    if (!slot.valid || slot.payloadLength < 24 ||
        slot.payloadLength > sizeof(ioBuffer_)) {
        return false;
    }
    fs::File file = fs_->open(slot.path, FILE_READ);
    if (!file || !file.seek(kRecordHeaderSize) ||
        file.read(ioBuffer_, slot.payloadLength) != slot.payloadLength) {
        if (file) {
            file.close();
        }
        return false;
    }
    file.close();

    PersistedSession& decoded = output == nullptr
                                    ? sessionDecodeScratch_
                                    : *output;
    return decodeSession(ioBuffer_, slot.payloadLength, decoded);
}

void PlayerStateStore::serviceQueuePrepare() {
    size_t processedBytes = 0;
    char path[kPersistedPathMaxBytes + 1];
    while (queuePrepareIndex_ < queueCount_ &&
           processedBytes < kStorageStepBytes) {
        if (!queueSource_->pathAt(
                queuePrepareIndex_, path, sizeof(path))) {
            complete(PersistenceResult::InvalidArgument);
            return;
        }
        const size_t pathLength = strnlen(path, sizeof(path));
        if (pathLength == 0 || pathLength > kPersistedPathMaxBytes ||
            !validUtf8Path(
                reinterpret_cast<const uint8_t*>(path), pathLength) ||
            preparedPayloadLength_ + sizeof(uint16_t) + pathLength >
                kPersistedQueueMaxPayloadBytes) {
            complete(PersistenceResult::InvalidArgument);
            return;
        }

        uint8_t lengthBytes[2];
        writeLe16(lengthBytes, static_cast<uint16_t>(pathLength));
        preparedPayloadCrcState_ = updateCrc32(
            preparedPayloadCrcState_, lengthBytes, sizeof(lengthBytes));
        preparedPayloadCrcState_ = updateCrc32(
            preparedPayloadCrcState_,
            reinterpret_cast<const uint8_t*>(path),
            pathLength);
        preparedPayloadLength_ += sizeof(uint16_t) + pathLength;
        processedBytes += sizeof(uint16_t) + pathLength;
        ++queuePrepareIndex_;
    }

    if (queuePrepareIndex_ == queueCount_) {
        preparedPayloadCrc_ = preparedPayloadCrcState_ ^ 0xFFFFFFFF;
        phase_ = JobPhase::OpenTarget;
    }
}

void PlayerStateStore::serviceOpenTarget() {
    if (!ensureStateDirectory()) {
        complete(PersistenceResult::IoError);
        return;
    }
    if (fs_->exists(targetPath_) && !fs_->remove(targetPath_)) {
        complete(PersistenceResult::IoError);
        return;
    }
    jobFile_ = fs_->open(targetPath_, FILE_WRITE);
    if (!jobFile_) {
        complete(PersistenceResult::IoError);
        return;
    }
    phase_ = JobPhase::WriteHeader;
}

void PlayerStateStore::serviceWriteHeader() {
    uint8_t header[kRecordHeaderSize] = {};
    writeLe32(
        header + 0,
        jobKind_ == PersistenceRecordKind::Queue ? kQueueMagic : kSessionMagic);
    writeLe16(header + 4, kFormatVersion);
    writeLe16(header + 6, kRecordHeaderSize);
    writeLe32(header + 8, jobGeneration_);
    writeLe32(header + 12, preparedPayloadLength_);
    writeLe32(header + 16, preparedPayloadCrc_);
    if (jobFile_.write(header, sizeof(header)) != sizeof(header)) {
        complete(PersistenceResult::IoError);
        return;
    }
    phase_ = JobPhase::WritePayload;
}

void PlayerStateStore::serviceWritePayload() {
    if (jobKind_ == PersistenceRecordKind::Session) {
        const size_t remaining = sessionPayloadLength_ - payloadWriteOffset_;
        const size_t request = std::min<size_t>(remaining, kStorageStepBytes);
        if (jobFile_.write(ioBuffer_ + payloadWriteOffset_, request) != request) {
            complete(PersistenceResult::IoError);
            return;
        }
        payloadWriteOffset_ += request;
        if (payloadWriteOffset_ == sessionPayloadLength_) {
            phase_ = JobPhase::CloseTarget;
        }
        return;
    }

    size_t buffered = 0;
    if (!queueCountWritten_) {
        writeLe16(ioBuffer_, queueCount_);
        buffered = sizeof(uint16_t);
        queueCountWritten_ = true;
    }

    char path[kPersistedPathMaxBytes + 1];
    while (queueWriteIndex_ < queueCount_) {
        if (!queueSource_->pathAt(queueWriteIndex_, path, sizeof(path))) {
            complete(PersistenceResult::InvalidArgument);
            return;
        }
        const size_t pathLength = strnlen(path, sizeof(path));
        const size_t recordLength = sizeof(uint16_t) + pathLength;
        if (pathLength == 0 || pathLength > kPersistedPathMaxBytes ||
            !validUtf8Path(
                reinterpret_cast<const uint8_t*>(path), pathLength)) {
            complete(PersistenceResult::InvalidArgument);
            return;
        }
        if (buffered + recordLength > kStorageStepBytes) {
            break;
        }

        writeLe16(ioBuffer_ + buffered, static_cast<uint16_t>(pathLength));
        buffered += sizeof(uint16_t);
        memcpy(ioBuffer_ + buffered, path, pathLength);
        buffered += pathLength;
        ++queueWriteIndex_;
    }

    if (buffered > 0 && jobFile_.write(ioBuffer_, buffered) != buffered) {
        complete(PersistenceResult::IoError);
        return;
    }
    if (queueWriteIndex_ == queueCount_) {
        phase_ = JobPhase::CloseTarget;
    }
}

void PlayerStateStore::serviceCloseTarget() {
    jobFile_.flush();
    jobFile_.close();
    phase_ = JobPhase::OpenVerify;
}

void PlayerStateStore::serviceOpenVerify() {
    jobFile_ = fs_->open(targetPath_, FILE_READ);
    uint8_t header[kRecordHeaderSize] = {};
    const uint32_t expectedMagic =
        jobKind_ == PersistenceRecordKind::Queue ? kQueueMagic : kSessionMagic;
    if (!jobFile_ || jobFile_.read(header, sizeof(header)) != sizeof(header) ||
        readLe32(header + 0) != expectedMagic ||
        readLe16(header + 4) != kFormatVersion ||
        readLe16(header + 6) != kRecordHeaderSize ||
        readLe32(header + 8) != jobGeneration_ ||
        readLe32(header + 12) != preparedPayloadLength_ ||
        readLe32(header + 16) != preparedPayloadCrc_ ||
        jobFile_.size() != kRecordHeaderSize + preparedPayloadLength_) {
        complete(PersistenceResult::Corrupt);
        return;
    }

    verifyRemaining_ = preparedPayloadLength_;
    verifyCrcState_ = 0xFFFFFFFF;
    phase_ = JobPhase::VerifyPayload;
}

void PlayerStateStore::serviceVerifyPayload() {
    if (verifyRemaining_ > 0) {
        const size_t request =
            std::min<size_t>(verifyRemaining_, kStorageStepBytes);
        const size_t bytesRead = jobFile_.read(ioBuffer_, request);
        if (bytesRead != request) {
            complete(PersistenceResult::IoError);
            return;
        }
        verifyCrcState_ = updateCrc32(
            verifyCrcState_, ioBuffer_, bytesRead);
        verifyRemaining_ -= bytesRead;
        return;
    }

    jobFile_.close();
    if ((verifyCrcState_ ^ 0xFFFFFFFF) != preparedPayloadCrc_) {
        complete(PersistenceResult::Corrupt);
        return;
    }
    complete(PersistenceResult::Ok);
}

void PlayerStateStore::complete(PersistenceResult result) {
    if (jobFile_) {
        jobFile_.close();
    }

    const PersistenceRecordKind completed = jobKind_;
    if (result == PersistenceResult::Ok) {
        SlotInfo slot;
        slot.valid = true;
        slot.generation = jobGeneration_;
        slot.payloadLength = preparedPayloadLength_;
        slot.payloadCrc = preparedPayloadCrc_;
        slot.path = targetPath_;
        slot.slot = targetSlot_;
        if (completed == PersistenceRecordKind::Queue) {
            currentQueueSlot_ = slot;
        } else if (completed == PersistenceRecordKind::Session) {
            currentSessionSlot_ = slot;
        }
    }

    completedKind_ = completed;
    lastResult_ = result;
    phase_ = JobPhase::Idle;
    jobKind_ = PersistenceRecordKind::None;
    queueSource_ = nullptr;
    targetPath_ = nullptr;
    targetSlot_ = 0;
    payloadWriteOffset_ = 0;
}

}  // namespace player
}  // namespace adv_walkman
