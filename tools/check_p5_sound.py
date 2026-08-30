"""P5 sound-preset contracts and coefficient reference checks."""

import cmath
import math
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def normalize(values, a0):
    return tuple(value / a0 for value in values)


def low_pass(rate, frequency, q):
    frequency = min(frequency, rate * 0.45)
    omega = 2 * math.pi * frequency / rate
    cosine, alpha = math.cos(omega), math.sin(omega) / (2 * q)
    return normalize(((1-cosine)/2, 1-cosine, (1-cosine)/2,
                      -2*cosine, 1-alpha), 1+alpha)


def high_pass(rate, frequency, q):
    frequency = min(frequency, rate * 0.20)
    omega = 2 * math.pi * frequency / rate
    cosine, alpha = math.cos(omega), math.sin(omega) / (2 * q)
    return normalize(((1+cosine)/2, -(1+cosine), (1+cosine)/2,
                      -2*cosine, 1-alpha), 1+alpha)


def peak(rate, frequency, q, gain_db):
    frequency = min(frequency, rate * 0.45)
    omega = 2 * math.pi * frequency / rate
    cosine, alpha = math.cos(omega), math.sin(omega) / (2*q)
    amplitude = 10 ** (gain_db / 40)
    return normalize((1+alpha*amplitude, -2*cosine,
                      1-alpha*amplitude, -2*cosine,
                      1-alpha/amplitude), 1+alpha/amplitude)


def shelf(rate, frequency, gain_db, high=False):
    frequency = min(frequency, rate * 0.45) if high else frequency
    amplitude = 10 ** (gain_db / 40)
    omega = 2 * math.pi * frequency / rate
    cosine = math.cos(omega)
    alpha = math.sin(omega) / math.sqrt(2)
    beta = 2 * math.sqrt(amplitude) * alpha
    if high:
        values = (amplitude*((amplitude+1)+(amplitude-1)*cosine+beta),
                  -2*amplitude*((amplitude-1)+(amplitude+1)*cosine),
                  amplitude*((amplitude+1)+(amplitude-1)*cosine-beta),
                  2*((amplitude-1)-(amplitude+1)*cosine),
                  (amplitude+1)-(amplitude-1)*cosine-beta)
        a0 = (amplitude+1)-(amplitude-1)*cosine+beta
    else:
        values = (amplitude*((amplitude+1)-(amplitude-1)*cosine+beta),
                  2*amplitude*((amplitude-1)-(amplitude+1)*cosine),
                  amplitude*((amplitude+1)-(amplitude-1)*cosine-beta),
                  -2*((amplitude-1)+(amplitude+1)*cosine),
                  (amplitude+1)+(amplitude-1)*cosine-beta)
        a0 = (amplitude+1)+(amplitude-1)*cosine+beta
    return normalize(values, a0)


def response(coefficients, rate, frequency):
    z1 = cmath.exp(-2j * math.pi * frequency / rate)
    z2 = z1 * z1
    b0, b1, b2, a1, a2 = coefficients
    return (b0+b1*z1+b2*z2) / (1+a1*z1+a2*z2)


def cascade_db(sections, rate, frequency):
    result = 1+0j
    for section in sections:
        result *= response(section, rate, frequency)
    return 20 * math.log10(abs(result))


class P5SoundChecks(unittest.TestCase):
    def test_production_path_is_in_place_and_retry_safe(self):
        output = source("src/player/audio/M5SpeakerPcmOutput.cpp")
        header = source("src/player/audio/M5SpeakerPcmOutput.h")
        self.assertIn("dsp_.processBlock(buffers_[activeBuffer_], frames)", output)
        self.assertIn("activeBufferProcessed_", output)
        self.assertLess(output.index("dsp_.processBlock"), output.index("speaker_->playRaw"))
        self.assertIn("kFramesPerBuffer = 1536", header)
        self.assertIn("kBufferCount = 3", header)

    def test_original_is_bit_exact_bypass(self):
        dsp = source("src/player/audio/PcmDsp.cpp")
        self.assertIn("Original is a strict, zero-cost bypass", dsp)
        self.assertIn("return;", dsp.split("Original is a strict, zero-cost bypass", 1)[1][:400])
        self.assertIn("-32768, -30000, -1, 0, 1, 30000, 32767", dsp)

    def test_four_player_only_direct_keys(self):
        keys = source("src/player/ui/PlayerKeys.h")
        controls = source("src/player/ui/P4Controls.h")
        for key in ("SoundOriginal", "SoundTape", "SoundRadio", "SoundVocalClear"):
            self.assertIn(key, keys)
        for action in ("SetSoundOriginal", "SetSoundTape", "SetSoundRadio",
                       "SetSoundVocalClear"):
            self.assertIn(action, controls)
        self.assertIn("P5 sound keys do not leak to other pages", controls)

    def test_session_v1_uses_only_reserved_byte_22(self):
        store = source("src/player/storage/PlayerStateStore.cpp")
        self.assertIn("output[22] = session.soundPreset", store)
        self.assertIn("output.soundPreset = input[22]", store)
        self.assertNotIn("output[23] =", store)
        runtime = source("src/player/app/PlayerRuntime.cpp")
        self.assertIn("restoredInvalidSoundPreset_ = session.soundPreset > 3", runtime)
        self.assertIn("output.soundPreset = static_cast<uint8_t>(soundPreset_)", runtime)

    def test_no_allocation_or_storage_in_sample_path(self):
        dsp = source("src/player/audio/PcmDsp.cpp")
        for forbidden in ("new ", "malloc", "calloc", "realloc", "SD.", "File"):
            self.assertNotIn(forbidden, dsp)
        header = source("src/player/audio/M5SpeakerPcmOutput.h")
        self.assertIn("dspRuntimeStateBytes() <= 256", header)

    def test_reference_frequency_shapes(self):
        rate, q = 44100, 1/math.sqrt(2)
        tape = (shelf(rate, 180, 1), shelf(rate, 4500, -3, True))
        radio = (high_pass(rate, 200, q), low_pass(rate, 5000, q))
        vocal = (shelf(rate, 180, -1), peak(rate, 1200, .8, 1),
                 peak(rate, 3000, 1, 2))
        self.assertGreater(cascade_db(tape, rate, 100), 0.5)
        self.assertLess(cascade_db(tape, rate, 10000), -2.0)
        self.assertLess(cascade_db(radio, rate, 80), -12)
        self.assertGreater(cascade_db(radio, rate, 1000), -1)
        self.assertLess(cascade_db(radio, rate, 10000), -10)
        self.assertGreater(cascade_db(vocal, rate, 3000), 1.5)

    def test_crossfade_limiter_and_diagnostics_are_wired(self):
        dsp = source("src/player/audio/PcmDsp.cpp")
        self.assertIn("sampleRateHz_ / 50U", dsp)
        self.assertIn("kLimiterCeiling = 0.891250938f", dsp)
        log = source("src/player/p3abc/FreeSession.cpp")
        for field in ("sound_preset_requests", "sound_footer_max_ms",
                      "sound_pcm_apply_max_us", "dsp_block_max_us",
                      "dsp_crossfades", "dsp_limiter_events",
                      "dsp_pre_limiter_peak_q15", "dsp_invalid_fallbacks",
                      "dsp_state_bytes"):
            self.assertIn(field, log)


if __name__ == "__main__":
    unittest.main(verbosity=2)
