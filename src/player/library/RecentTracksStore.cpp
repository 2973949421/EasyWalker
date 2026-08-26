#include "RecentTracksStore.h"

#include <algorithm>
#include <string.h>

namespace adv_walkman {
namespace player {

namespace {

constexpr uint32_t kRecentMagic = 0x31525741;  // "AWR1" on disk.
constexpr uint16_t kFormatVersion = 1;
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

uint8_t foldAscii(uint8_t value) {
    return value >= 'A' && value <= 'Z'
               ? static_cast<uint8_t>(value + ('a' - 'A'))
               : value;
}

bool pathEqualsAsciiCaseInsensitive(
    const uint8_t* left,
    size_t leftLength,
    const char* right,
    size_t rightLength) {
    if (leftLength != rightLength) {
        return false;
    }
    for (size_t index = 0; index < leftLength; ++index) {
        if (foldAscii(left[index]) !=
            foldAscii(static_cast<uint8_t>(right[index]))) {
            return false;
        }
    }
    return true;
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
        for (size_t remaining = 1; remaining < trailing;
             ++remaining, ++index) {
            if ((data[index] & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

bool canonicalAbsolutePath(const char* path, size_t length) {
    if (path == nullptr || length < 2 || path[0] != '/' ||
        path[length - 1] == '/') {
        return false;
    }

    size_t componentStart = 1;
    for (size_t index = 1; index <= length; ++index) {
        if (index < length && path[index] == '\\') {
            return false;
        }
        if (index < length && path[index] != '/') {
            continue;
        }

        const size_t componentLength = index - componentStart;
        if (componentLength == 0 ||
            (componentLength == 1 && path[componentStart] == '.') ||
            (componentLength == 2 && path[componentStart] == '.' &&
             path[componentStart + 1] == '.')) {
            return false;
        }
        componentStart = index + 1;
    }
    return validUtf8Path(
        reinterpret_cast<const uint8_t*>(path), length);
}

bool pathIsExistingFile(fs::FS& fs, const char* path) {
    if (!fs.exists(path)) {
        return false;
    }
    fs::File file = fs.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    const bool isFile = !file.isDirectory();
    file.close();
    return isFile;
}

}  // namespace

const char* const RecentTracksStore::kRecentSlotA =
    "/ADVWalkman/state/recent-a.bin";
const char* const RecentTracksStore::kRecentSlotB =
    "/ADVWalkman/state/recent-b.bin";

const char* recentTracksResultName(RecentTracksResult result) {
    switch (result) {
        case RecentTracksResult::Ok:
            return "OK";
        case RecentTracksResult::Pending:
            return "PENDING";
        case RecentTracksResult::NotFound:
            return "NOT_FOUND";
        case RecentTracksResult::NotLoaded:
            return "NOT_LOADED";
        case RecentTracksResult::Busy:
            return "BUSY";
        case RecentTracksResult::InvalidArgument:
            return "INVALID_ARGUMENT";
        case RecentTracksResult::UnsupportedVersion:
            return "UNSUPPORTED_VERSION";
        case RecentTracksResult::Corrupt:
            return "CORRUPT";
        case RecentTracksResult::IoError:
            return "IO_ERROR";
        default:
            return "UNKNOWN";
    }
}

bool RecentTracksStore::begin(
    fs::FS& fs,
    const char* slotA,
    const char* slotB) {
    if (pending()) {
        return false;
    }
    loaded_ = false;
    if (slotA == nullptr || slotB == nullptr) {
        lastResult_ = RecentTracksResult::InvalidArgument;
        return false;
    }
    const size_t slotALength = strnlen(slotA, sizeof(slotPathA_) + 1);
    const size_t slotBLength = strnlen(slotB, sizeof(slotPathB_) + 1);
    if (slotALength < 2 || slotBLength < 2 ||
        slotALength >= sizeof(slotPathA_) ||
        slotBLength >= sizeof(slotPathB_) || slotA[0] != '/' ||
        slotB[0] != '/' || !canonicalAbsolutePath(slotA, slotALength) ||
        !canonicalAbsolutePath(slotB, slotBLength) ||
        pathEqualsAsciiCaseInsensitive(
            reinterpret_cast<const uint8_t*>(slotA), slotALength,
            slotB, slotBLength)) {
        lastResult_ = RecentTracksResult::InvalidArgument;
        return false;
    }

    fs_ = &fs;
    memcpy(slotPathA_, slotA, slotALength + 1);
    memcpy(slotPathB_, slotB, slotBLength + 1);
    currentSlot_ = SlotInfo{};
    entryBytes_ = 0;
    count_ = 0;
    loaded_ = false;
    dirty_ = false;
    lastResult_ = ensureStorageDirectories() ? RecentTracksResult::Ok
                                              : RecentTracksResult::IoError;
    return lastResult_ == RecentTracksResult::Ok;
}

RecentTracksResult RecentTracksStore::load() {
    if (pending()) {
        return RecentTracksResult::Busy;
    }
    entryBytes_ = 0;
    count_ = 0;
    loaded_ = false;
    dirty_ = false;
    currentSlot_ = SlotInfo{};
    if (fs_ == nullptr) {
        return lastResult_ = RecentTracksResult::IoError;
    }

    SlotInfo slotA;
    SlotInfo slotB;
    slotA.path = slotPathA_;
    slotA.slot = 'A';
    slotB.path = slotPathB_;
    slotB.slot = 'B';
    bool unsupportedA = false;
    bool unsupportedB = false;
    slotA.valid = inspectRecord(slotPathA_, slotA, unsupportedA) &&
                  validatePayload(slotA);
    slotB.valid = inspectRecord(slotPathB_, slotB, unsupportedB) &&
                  validatePayload(slotB);

    if (!slotA.valid && !slotB.valid) {
        const bool sawRecord = fs_->exists(slotPathA_) ||
                               fs_->exists(slotPathB_);
        if (unsupportedA || unsupportedB) {
            return lastResult_ = RecentTracksResult::UnsupportedVersion;
        }
        if (sawRecord) {
            return lastResult_ = RecentTracksResult::Corrupt;
        }
        loaded_ = true;
        return lastResult_ = RecentTracksResult::NotFound;
    }

    currentSlot_ = slotA.valid &&
                           (!slotB.valid || generationIsNewer(
                                                slotA.generation,
                                                slotB.generation))
                       ? slotA
                       : slotB;
    bool removedMissingOrDuplicate = false;
    if (!loadPayload(currentSlot_, removedMissingOrDuplicate)) {
        entryBytes_ = 0;
        count_ = 0;
        currentSlot_ = SlotInfo{};
        return lastResult_ = RecentTracksResult::Corrupt;
    }

    loaded_ = true;
    lastResult_ = RecentTracksResult::Ok;
    if (removedMissingOrDuplicate) {
        dirty_ = true;
        startSave();
    }
    return lastResult_;
}

RecentTracksResult RecentTracksStore::retrySave() {
    if (!loaded_) {
        return lastResult_ = RecentTracksResult::NotLoaded;
    }
    if (pending()) {
        return RecentTracksResult::Busy;
    }
    if (!dirty_) {
        return lastResult_ = RecentTracksResult::Ok;
    }
    startSave();
    return lastResult_ = RecentTracksResult::Pending;
}

RecentTracksResult RecentTracksStore::record(const char* path) {
    if (!loaded_) {
        return lastResult_ = RecentTracksResult::NotLoaded;
    }
    if (pending()) {
        return RecentTracksResult::Busy;
    }
    if (fs_ == nullptr) {
        return lastResult_ = RecentTracksResult::IoError;
    }
    if (path == nullptr) {
        return lastResult_ = RecentTracksResult::InvalidArgument;
    }
    const size_t pathLength = strnlen(path, kMaxTrackPathBytes + 2);
    if (pathLength == 0 || pathLength > kMaxTrackPathBytes ||
        !canonicalAbsolutePath(path, pathLength)) {
        return lastResult_ = RecentTracksResult::InvalidArgument;
    }
    if (!fs_->exists(path)) {
        return lastResult_ = RecentTracksResult::NotFound;
    }
    if (!pathIsExistingFile(*fs_, path)) {
        return lastResult_ = RecentTracksResult::InvalidArgument;
    }

    size_t existingOffset = 0;
    size_t existingLength = 0;
    size_t existingIndex = 0;
    const bool found = findPath(
        path, pathLength, existingOffset, existingLength, existingIndex);
    if (found && existingIndex == 0) {
        if (dirty_) {
            startSave();
            return lastResult_ = RecentTracksResult::Pending;
        }
        return lastResult_ = RecentTracksResult::Ok;
    }

    if (found) {
        memmove(entries_ + existingOffset,
                entries_ + existingOffset + existingLength,
                entryBytes_ - existingOffset - existingLength);
        entryBytes_ -= existingLength;
        --count_;
    } else if (count_ == kMaximumTracks) {
        size_t lastOffset = 0;
        uint16_t lastPathLength = 0;
        if (!locateRecord(count_ - 1, lastOffset, lastPathLength)) {
            return lastResult_ = RecentTracksResult::Corrupt;
        }
        entryBytes_ = static_cast<uint16_t>(lastOffset);
        --count_;
    }

    const size_t newRecordLength = sizeof(uint16_t) + pathLength;
    if (entryBytes_ + newRecordLength > sizeof(entries_)) {
        return lastResult_ = RecentTracksResult::InvalidArgument;
    }
    memmove(entries_ + newRecordLength, entries_, entryBytes_);
    writeLe16(entries_, static_cast<uint16_t>(pathLength));
    memcpy(entries_ + sizeof(uint16_t), path, pathLength);
    entryBytes_ += static_cast<uint16_t>(newRecordLength);
    ++count_;

    dirty_ = true;
    startSave();
    return lastResult_ = RecentTracksResult::Pending;
}

void RecentTracksStore::service() {
    switch (phase_) {
        case JobPhase::Idle:
            return;
        case JobPhase::PrepareCrc:
            servicePrepareCrc();
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

bool RecentTracksStore::pending() const {
    return phase_ != JobPhase::Idle;
}

bool RecentTracksStore::loaded() const {
    return loaded_;
}

bool RecentTracksStore::dirty() const {
    return dirty_;
}

RecentTracksResult RecentTracksStore::lastResult() const {
    return lastResult_;
}

uint32_t RecentTracksStore::generation() const {
    return currentSlot_.generation;
}

size_t RecentTracksStore::count() const {
    return count_;
}

bool RecentTracksStore::pathAt(
    size_t index,
    char* output,
    size_t outputCapacity) const {
    if (output == nullptr || outputCapacity == 0) {
        return false;
    }
    output[0] = '\0';
    size_t recordOffset = 0;
    uint16_t pathLength = 0;
    if (!locateRecord(index, recordOffset, pathLength) ||
        outputCapacity <= pathLength) {
        return false;
    }
    memcpy(output, entries_ + recordOffset + sizeof(uint16_t), pathLength);
    output[pathLength] = '\0';
    return true;
}

bool RecentTracksStore::ensureStorageDirectories() {
    if (fs_ == nullptr) {
        return false;
    }
    return ensureParentDirectory(slotPathA_) &&
           ensureParentDirectory(slotPathB_);
}

bool RecentTracksStore::ensureParentDirectory(const char* filePath) {
    if (fs_ == nullptr || filePath == nullptr || filePath[0] != '/') {
        return false;
    }
    const size_t pathLength = strnlen(filePath, kTrackPathCapacity);
    if (pathLength < 2 || pathLength >= kTrackPathCapacity) {
        return false;
    }

    size_t parentLength = pathLength;
    while (parentLength > 0 && filePath[parentLength - 1] != '/') {
        --parentLength;
    }
    if (parentLength <= 1) {
        return true;
    }
    --parentLength;

    char directory[kTrackPathCapacity];
    memcpy(directory, filePath, parentLength);
    directory[parentLength] = '\0';
    for (size_t index = 1; index <= parentLength; ++index) {
        if (index != parentLength && directory[index] != '/') {
            continue;
        }
        const char saved = directory[index];
        directory[index] = '\0';
        const bool exists = fs_->exists(directory);
        const bool created = exists || fs_->mkdir(directory);
        directory[index] = saved;
        if (!created) {
            return false;
        }
    }
    return true;
}

bool RecentTracksStore::inspectRecord(
    const char* path,
    SlotInfo& output,
    bool& unsupportedVersion) {
    unsupportedVersion = false;
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
    unsupportedVersion = magic == kRecentMagic && version != kFormatVersion;
    if (magic != kRecentMagic || version != kFormatVersion ||
        headerSize != kRecordHeaderSize || generation == 0 ||
        payloadLength < sizeof(uint16_t) ||
        payloadLength > kMaximumPayloadBytes ||
        file.size() != kRecordHeaderSize + payloadLength) {
        file.close();
        return false;
    }

    uint32_t remaining = payloadLength;
    uint32_t crcState = 0xFFFFFFFF;
    while (remaining > 0) {
        const size_t request =
            std::min<size_t>(remaining, sizeof(ioBuffer_));
        const size_t bytesRead = file.read(ioBuffer_, request);
        if (bytesRead != request) {
            file.close();
            return false;
        }
        crcState = updateCrc32(crcState, ioBuffer_, bytesRead);
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

bool RecentTracksStore::validatePayload(const SlotInfo& slot) {
    if (!slot.valid) {
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
    const uint16_t storedCount = readLe16(pair);
    if (storedCount > kMaximumTracks) {
        file.close();
        return false;
    }

    uint32_t consumed = sizeof(uint16_t);
    for (uint16_t index = 0; index < storedCount; ++index) {
        if (file.read(pair, sizeof(pair)) != sizeof(pair)) {
            file.close();
            return false;
        }
        const uint16_t pathLength = readLe16(pair);
        consumed += sizeof(uint16_t);
        if (pathLength == 0 || pathLength > kMaxTrackPathBytes ||
            consumed + pathLength > slot.payloadLength ||
            file.read(ioBuffer_, pathLength) != pathLength ||
            !validUtf8Path(ioBuffer_, pathLength)) {
            file.close();
            return false;
        }
        consumed += pathLength;
    }
    file.close();
    return consumed == slot.payloadLength;
}

bool RecentTracksStore::loadPayload(
    const SlotInfo& slot,
    bool& removedMissingOrDuplicate) {
    removedMissingOrDuplicate = false;
    entryBytes_ = 0;
    count_ = 0;

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
    const uint16_t storedCount = readLe16(pair);
    uint32_t consumed = sizeof(uint16_t);
    for (uint16_t index = 0; index < storedCount; ++index) {
        if (file.read(pair, sizeof(pair)) != sizeof(pair)) {
            file.close();
            return false;
        }
        const uint16_t pathLength = readLe16(pair);
        consumed += sizeof(uint16_t);
        if (pathLength == 0 || pathLength > kMaxTrackPathBytes ||
            consumed + pathLength > slot.payloadLength ||
            file.read(ioBuffer_, pathLength) != pathLength ||
            !validUtf8Path(ioBuffer_, pathLength)) {
            file.close();
            return false;
        }
        consumed += pathLength;
        ioBuffer_[pathLength] = 0;

        size_t duplicateOffset = 0;
        size_t duplicateLength = 0;
        size_t duplicateIndex = 0;
        const char* path = reinterpret_cast<const char*>(ioBuffer_);
        if (!canonicalAbsolutePath(path, pathLength) ||
            !pathIsExistingFile(*fs_, path) ||
            findPath(path, pathLength, duplicateOffset, duplicateLength,
                     duplicateIndex)) {
            removedMissingOrDuplicate = true;
            continue;
        }

        const size_t recordLength = sizeof(uint16_t) + pathLength;
        if (count_ >= kMaximumTracks ||
            entryBytes_ + recordLength > sizeof(entries_)) {
            file.close();
            return false;
        }
        writeLe16(entries_ + entryBytes_, pathLength);
        memcpy(entries_ + entryBytes_ + sizeof(uint16_t), path, pathLength);
        entryBytes_ += static_cast<uint16_t>(recordLength);
        ++count_;
    }
    file.close();
    return consumed == slot.payloadLength;
}

bool RecentTracksStore::findPath(
    const char* path,
    size_t pathLength,
    size_t& recordOffset,
    size_t& recordLength,
    size_t& recordIndex) const {
    size_t offset = 0;
    for (size_t index = 0; index < count_; ++index) {
        if (offset + sizeof(uint16_t) > entryBytes_) {
            return false;
        }
        const uint16_t storedLength = readLe16(entries_ + offset);
        const size_t length = sizeof(uint16_t) + storedLength;
        if (storedLength == 0 || offset + length > entryBytes_) {
            return false;
        }
        if (pathEqualsAsciiCaseInsensitive(
                entries_ + offset + sizeof(uint16_t), storedLength,
                path, pathLength)) {
            recordOffset = offset;
            recordLength = length;
            recordIndex = index;
            return true;
        }
        offset += length;
    }
    return false;
}

bool RecentTracksStore::locateRecord(
    size_t index,
    size_t& recordOffset,
    uint16_t& pathLength) const {
    if (index >= count_) {
        return false;
    }
    size_t offset = 0;
    for (size_t current = 0; current <= index; ++current) {
        if (offset + sizeof(uint16_t) > entryBytes_) {
            return false;
        }
        pathLength = readLe16(entries_ + offset);
        const size_t recordLength = sizeof(uint16_t) + pathLength;
        if (pathLength == 0 || offset + recordLength > entryBytes_) {
            return false;
        }
        if (current == index) {
            recordOffset = offset;
            return true;
        }
        offset += recordLength;
    }
    return false;
}

void RecentTracksStore::startSave() {
    jobGeneration_ = nextGeneration(currentSlot_.generation);
    if (currentSlot_.valid && currentSlot_.slot == 'A') {
        targetPath_ = slotPathB_;
        targetSlot_ = 'B';
    } else {
        targetPath_ = slotPathA_;
        targetSlot_ = 'A';
    }
    preparedPayloadLength_ = sizeof(uint16_t) + entryBytes_;
    preparedPayloadCrc_ = 0;
    prepareOffset_ = 0;
    prepareCrcState_ = 0xFFFFFFFF;
    writeOffset_ = 0;
    phase_ = JobPhase::PrepareCrc;
}

size_t RecentTracksStore::copyPayloadBytes(
    uint32_t offset,
    uint8_t* output,
    size_t length) const {
    if (output == nullptr || offset >= preparedPayloadLength_) {
        return 0;
    }
    const size_t available = preparedPayloadLength_ - offset;
    size_t remaining = std::min(length, available);
    size_t copied = 0;
    uint8_t countBytes[2];
    writeLe16(countBytes, count_);
    if (offset < sizeof(countBytes)) {
        const size_t countPart = std::min(
            remaining, sizeof(countBytes) - static_cast<size_t>(offset));
        memcpy(output, countBytes + offset, countPart);
        offset += countPart;
        copied += countPart;
        remaining -= countPart;
    }
    if (remaining > 0) {
        const size_t entriesOffset = offset - sizeof(countBytes);
        memcpy(output + copied, entries_ + entriesOffset, remaining);
        copied += remaining;
    }
    return copied;
}

void RecentTracksStore::servicePrepareCrc() {
    const size_t copied = copyPayloadBytes(
        prepareOffset_, ioBuffer_, sizeof(ioBuffer_));
    if (copied == 0) {
        complete(RecentTracksResult::Corrupt);
        return;
    }
    prepareCrcState_ = updateCrc32(prepareCrcState_, ioBuffer_, copied);
    prepareOffset_ += copied;
    if (prepareOffset_ == preparedPayloadLength_) {
        preparedPayloadCrc_ = prepareCrcState_ ^ 0xFFFFFFFF;
        phase_ = JobPhase::OpenTarget;
    }
}

void RecentTracksStore::serviceOpenTarget() {
    if (!ensureStorageDirectories()) {
        complete(RecentTracksResult::IoError);
        return;
    }
    if (fs_->exists(targetPath_) && !fs_->remove(targetPath_)) {
        complete(RecentTracksResult::IoError);
        return;
    }
    jobFile_ = fs_->open(targetPath_, FILE_WRITE);
    if (!jobFile_) {
        complete(RecentTracksResult::IoError);
        return;
    }
    phase_ = JobPhase::WriteHeader;
}

void RecentTracksStore::serviceWriteHeader() {
    uint8_t header[kRecordHeaderSize] = {};
    writeLe32(header + 0, kRecentMagic);
    writeLe16(header + 4, kFormatVersion);
    writeLe16(header + 6, kRecordHeaderSize);
    writeLe32(header + 8, jobGeneration_);
    writeLe32(header + 12, preparedPayloadLength_);
    writeLe32(header + 16, preparedPayloadCrc_);
    if (jobFile_.write(header, sizeof(header)) != sizeof(header)) {
        complete(RecentTracksResult::IoError);
        return;
    }
    phase_ = JobPhase::WritePayload;
}

void RecentTracksStore::serviceWritePayload() {
    const size_t copied =
        copyPayloadBytes(writeOffset_, ioBuffer_, sizeof(ioBuffer_));
    if (copied == 0 || jobFile_.write(ioBuffer_, copied) != copied) {
        complete(RecentTracksResult::IoError);
        return;
    }
    writeOffset_ += copied;
    if (writeOffset_ == preparedPayloadLength_) {
        phase_ = JobPhase::CloseTarget;
    }
}

void RecentTracksStore::serviceCloseTarget() {
    jobFile_.flush();
    jobFile_.close();
    phase_ = JobPhase::OpenVerify;
}

void RecentTracksStore::serviceOpenVerify() {
    jobFile_ = fs_->open(targetPath_, FILE_READ);
    uint8_t header[kRecordHeaderSize] = {};
    if (!jobFile_ ||
        jobFile_.read(header, sizeof(header)) != sizeof(header) ||
        readLe32(header + 0) != kRecentMagic ||
        readLe16(header + 4) != kFormatVersion ||
        readLe16(header + 6) != kRecordHeaderSize ||
        readLe32(header + 8) != jobGeneration_ ||
        readLe32(header + 12) != preparedPayloadLength_ ||
        readLe32(header + 16) != preparedPayloadCrc_ ||
        jobFile_.size() != kRecordHeaderSize + preparedPayloadLength_) {
        complete(RecentTracksResult::Corrupt);
        return;
    }
    verifyRemaining_ = preparedPayloadLength_;
    verifyCrcState_ = 0xFFFFFFFF;
    phase_ = JobPhase::VerifyPayload;
}

void RecentTracksStore::serviceVerifyPayload() {
    if (verifyRemaining_ > 0) {
        const size_t request =
            std::min<size_t>(verifyRemaining_, sizeof(ioBuffer_));
        const size_t bytesRead = jobFile_.read(ioBuffer_, request);
        if (bytesRead != request) {
            complete(RecentTracksResult::IoError);
            return;
        }
        verifyCrcState_ = updateCrc32(
            verifyCrcState_, ioBuffer_, bytesRead);
        verifyRemaining_ -= bytesRead;
        return;
    }

    jobFile_.close();
    if ((verifyCrcState_ ^ 0xFFFFFFFF) != preparedPayloadCrc_) {
        complete(RecentTracksResult::Corrupt);
        return;
    }
    complete(RecentTracksResult::Ok);
}

void RecentTracksStore::complete(RecentTracksResult result) {
    if (jobFile_) {
        jobFile_.close();
    }
    if (result == RecentTracksResult::Ok) {
        currentSlot_.valid = true;
        currentSlot_.generation = jobGeneration_;
        currentSlot_.payloadLength = preparedPayloadLength_;
        currentSlot_.payloadCrc = preparedPayloadCrc_;
        currentSlot_.path = targetPath_;
        currentSlot_.slot = targetSlot_;
        dirty_ = false;
    }
    lastResult_ = result;
    phase_ = JobPhase::Idle;
    targetPath_ = nullptr;
    targetSlot_ = 0;
    prepareOffset_ = 0;
    writeOffset_ = 0;
}

}  // namespace player
}  // namespace adv_walkman
