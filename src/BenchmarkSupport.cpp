#include "BenchmarkSupport.h"

#include <SD.h>
#include <SPI.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

namespace adv_walkman {

namespace {

constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs = 12;
constexpr uint32_t kSdFrequencyHz = 25000000UL;
bool sdMounted = false;

int sha256Starts(mbedtls_sha256_context* context) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_starts(context, 0);
#else
    return mbedtls_sha256_starts_ret(context, 0);
#endif
}

int sha256Update(mbedtls_sha256_context* context,
                 const unsigned char* input, size_t length) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_update(context, input, length);
#else
    return mbedtls_sha256_update_ret(context, input, length);
#endif
}

int sha256Finish(mbedtls_sha256_context* context, unsigned char* output) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_finish(context, output);
#else
    return mbedtls_sha256_finish_ret(context, output);
#endif
}

constexpr int16_t constexprDownmix(int16_t left, int16_t right) {
    return static_cast<int16_t>(
        (static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
}

static_assert(constexprDownmix(32767, 32767) == 32767,
              "positive full-scale downmix must not overflow");
static_assert(constexprDownmix(-32768, -32768) == -32768,
              "negative full-scale downmix must not overflow");
static_assert(constexprDownmix(32767, -32768) == 0,
              "opposite-polarity channels should cancel around zero");

}  // namespace

int16_t downmixStereoToMono(int16_t left, int16_t right) {
    return constexprDownmix(left, right);
}

uint32_t benchmarkByteOffsetForSeconds(uint32_t seconds, uint32_t fileSize) {
    const uint64_t requested =
        static_cast<uint64_t>(seconds) * kBenchmarkBitrateBitsPerSecond / 8;
    if (fileSize <= 1) {
        return 0;
    }
    return static_cast<uint32_t>(
        requested < fileSize ? requested : static_cast<uint64_t>(fileSize - 1));
}

bool mountBenchmarkSd() {
    if (sdMounted) {
        return true;
    }
    // M5Cardputer's official Cardputer ADV examples use this SPI mapping.
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    sdMounted = SD.begin(kSdCs, SPI, kSdFrequencyHz);
    return sdMounted;
}

bool computeBenchmarkFileSha256(const char* path, char output[65]) {
    File input = SD.open(path, FILE_READ);
    if (!input) {
        return false;
    }

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (sha256Starts(&context) != 0) {
        mbedtls_sha256_free(&context);
        input.close();
        return false;
    }

    uint8_t buffer[1024];
    while (input.available()) {
        const size_t count = input.read(buffer, sizeof(buffer));
        if (count == 0 || sha256Update(&context, buffer, count) != 0) {
            mbedtls_sha256_free(&context);
            input.close();
            return false;
        }
    }

    uint8_t digest[32];
    const int result = sha256Finish(&context, digest);
    mbedtls_sha256_free(&context);
    input.close();
    if (result != 0) {
        return false;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t index = 0; index < sizeof(digest); ++index) {
        output[index * 2] = kHex[digest[index] >> 4];
        output[index * 2 + 1] = kHex[digest[index] & 0x0F];
    }
    output[64] = '\0';
    return true;
}

}  // namespace adv_walkman
