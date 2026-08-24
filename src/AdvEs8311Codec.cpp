#include "AdvEs8311Codec.h"

#include <M5Unified.h>

namespace adv_walkman {

bool AdvEs8311Codec::begin() {
    // Exact Cardputer ADV speaker-enable sequence from M5Unified 0.2.20.
    // The codec uses BCLK as MCLK; address 0x18 is on M5.In_I2C.
    static constexpr uint8_t kAddress = 0x18;
    static constexpr uint32_t kFrequency = 100000;
    static constexpr uint8_t kRegisters[][2] = {
        {0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18},
        {0x0D, 0x01}, {0x12, 0x00}, {0x13, 0x10},
        {0x32, 0xBF}, {0x37, 0x08},
    };

    for (const auto& item : kRegisters) {
        if (!M5.In_I2C.writeRegister8(kAddress, item[0], item[1], kFrequency)) {
            return false;
        }
    }
    return true;
}

void AdvEs8311Codec::end() {
    // M5Unified 0.2.20 intentionally has no Cardputer ADV disable sequence.
    // I2S is stopped by the backend; do not invent unverified power-down writes.
}

}  // namespace adv_walkman
