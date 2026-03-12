#include "audio_task.h"

#include "config.h"
#include "ota_manager.h"
#include "platform_profile.h"

#include <driver/i2s.h>
#include <SPIFFS.h>
#include <FS.h>

#include <cstring>

namespace {

constexpr i2s_port_t AUDIO_I2S_PORT = I2S_NUM_0;
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint16_t AUDIO_CHUNK_SAMPLES = 256;
constexpr uint8_t AUDIO_TASK_CORE = 1;
constexpr UBaseType_t AUDIO_TASK_PRIO = 1;

enum class AudioCommandType : uint8_t {
    PlayTestFallback,
    PlayTestTone,
    PlaySfx,
    Stop
};

struct AudioCommand {
    AudioCommandType type;
    uint16_t freqHz;
    uint16_t durationMs;
    uint8_t sfxId;
};

struct ToneState {
    bool active = false;
    uint16_t freqHz = 0;
    uint32_t remainingSamples = 0;
    uint32_t phase = 0;
};

struct PcmPlaybackState {
    bool active = false;
    int16_t* samples = nullptr;
    size_t framesTotal = 0;
    size_t framePos = 0;
    uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    uint8_t channels = 1; // 1 = mono, 2 = stereo
    AudioTestSource source = AudioTestSource::None;
};

struct LoadedPcmBuffer {
    int16_t* samples = nullptr;
    size_t frames = 0;
    uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    uint8_t channels = 1;
};

static constexpr char FLASH_RING_WAV[] = "/ESP32_onboard_ring.wav";
static constexpr char SFX_BLE_ON[] = "/sfx_ble_on.wav";
static constexpr char SFX_BLE_OFF[] = "/sfx_ble_off.wav";
static constexpr char SFX_BLE_CONNECTED[] = "/sfx_ble_connected.wav";
static constexpr char SFX_BLE_DISCONNECTED[] = "/sfx_ble_disconnected.wav";
static constexpr char SFX_OK[] = "/sfx_ok.wav";
static constexpr char SFX_ERROR[] = "/sfx_error.wav";
static constexpr size_t MAX_TEST_AUDIO_BYTES = 512 * 1024;

static TaskHandle_t g_audioTaskHandle = nullptr;
static volatile bool g_audioTaskRunning = false;
static volatile bool g_audioPlaybackActive = false;
static QueueHandle_t g_audioQueue = nullptr;
static bool g_i2sReady = false;
static bool g_flashFsReady = false;
static AudioTestSource g_lastTestSource = AudioTestSource::None;

static void resetPcmState(PcmPlaybackState& pcm) {
    if (pcm.samples != nullptr) {
        free(pcm.samples);
        pcm.samples = nullptr;
    }
    pcm.active = false;
    pcm.framesTotal = 0;
    pcm.framePos = 0;
    pcm.sampleRate = AUDIO_SAMPLE_RATE;
    pcm.channels = 1;
    pcm.source = AudioTestSource::None;
}

static bool ensureFlashFsMounted() {
    if (g_flashFsReady) {
        return true;
    }

    if (!SPIFFS.begin(false)) {
        Serial.print("\n[AUDIO] SPIFFS mount failed");
        return false;
    }

    g_flashFsReady = true;
    return true;
}

static bool initI2S() {
    if (g_i2sReady) {
        return true;
    }

    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = AUDIO_SAMPLE_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    // Keep classic I2S frame format without using deprecated symbol aliases.
    config.communication_format = static_cast<i2s_comm_format_t>(0x01);
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = AUDIO_I2S_BCLK_PIN;
    pins.ws_io_num = AUDIO_I2S_LRCLK_PIN;
    pins.data_out_num = AUDIO_I2S_DOUT_PIN;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(AUDIO_I2S_PORT, &config, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("\n[AUDIO] I2S install failed: %d", static_cast<int>(err));
        return false;
    }

    err = i2s_set_pin(AUDIO_I2S_PORT, &pins);
    if (err != ESP_OK) {
        Serial.printf("\n[AUDIO] I2S pin config failed: %d", static_cast<int>(err));
        i2s_driver_uninstall(AUDIO_I2S_PORT);
        return false;
    }

    g_i2sReady = true;
    Serial.printf("\n[AUDIO] I2S ready: BCLK=%d WS=%d DOUT=%d", AUDIO_I2S_BCLK_PIN, AUDIO_I2S_LRCLK_PIN, AUDIO_I2S_DOUT_PIN);
    return true;
}

static bool setI2SRate(uint32_t sampleRate) {
    if (!g_i2sReady) {
        return false;
    }

    esp_err_t err = i2s_set_clk(
        AUDIO_I2S_PORT,
        sampleRate,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO
    );

    if (err != ESP_OK) {
        Serial.printf("\n[AUDIO] I2S set_clk failed: %d", static_cast<int>(err));
        return false;
    }

    return true;
}

static void stopI2S() {
    if (!g_i2sReady) {
        return;
    }

    i2s_zero_dma_buffer(AUDIO_I2S_PORT);
    i2s_driver_uninstall(AUDIO_I2S_PORT);
    g_i2sReady = false;
}

static bool loadWavFromFile(const char* path, LoadedPcmBuffer& outBuffer) {
    if (!ensureFlashFsMounted()) {
        return false;
    }

    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        return false;
    }

    if (f.size() < 44 || static_cast<size_t>(f.size()) > MAX_TEST_AUDIO_BYTES) {
        f.close();
        return false;
    }

    uint8_t header[44] = {0};
    if (f.read(header, sizeof(header)) != sizeof(header)) {
        f.close();
        return false;
    }

    const bool riffOk = std::memcmp(header + 0, "RIFF", 4) == 0;
    const bool waveOk = std::memcmp(header + 8, "WAVE", 4) == 0;
    const bool fmtOk = std::memcmp(header + 12, "fmt ", 4) == 0;
    const bool dataOk = std::memcmp(header + 36, "data", 4) == 0;
    if (!riffOk || !waveOk || !fmtOk || !dataOk) {
        f.close();
        Serial.print("\n[AUDIO] WAV header unsupported");
        return false;
    }

    auto rd16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(header[off] | (header[off + 1] << 8));
    };
    auto rd32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(header[off]) |
               (static_cast<uint32_t>(header[off + 1]) << 8) |
               (static_cast<uint32_t>(header[off + 2]) << 16) |
               (static_cast<uint32_t>(header[off + 3]) << 24);
    };

    const uint16_t audioFormat = rd16(20);
    const uint16_t channels = rd16(22);
    const uint32_t sampleRate = rd32(24);
    const uint16_t bits = rd16(34);
    const uint32_t dataSize = rd32(40);

    if (audioFormat != 1 || (channels != 1 && channels != 2) || bits != 16 || dataSize == 0 || (dataSize % 2) != 0) {
        f.close();
        Serial.print("\n[AUDIO] WAV format must be PCM16 mono/stereo");
        return false;
    }

    if (dataSize > MAX_TEST_AUDIO_BYTES) {
        f.close();
        Serial.print("\n[AUDIO] WAV too large");
        return false;
    }

    int16_t* data = static_cast<int16_t*>(malloc(dataSize));
    if (!data) {
        f.close();
        Serial.print("\n[AUDIO] WAV alloc failed");
        return false;
    }

    const size_t readBytes = f.read(reinterpret_cast<uint8_t*>(data), dataSize);
    f.close();

    if (readBytes != dataSize) {
        free(data);
        Serial.print("\n[AUDIO] WAV read failed");
        return false;
    }

    outBuffer.samples = data;
    outBuffer.channels = static_cast<uint8_t>(channels);
    outBuffer.sampleRate = sampleRate;
    outBuffer.frames = dataSize / (sizeof(int16_t) * channels);
    return true;
}

static bool selectFallbackSource(LoadedPcmBuffer& outBuffer, AudioTestSource& sourceOut) {
    if (loadWavFromFile(FLASH_RING_WAV, outBuffer)) {
        sourceOut = AudioTestSource::FlashWav;
        return true;
    }

    sourceOut = AudioTestSource::Tone;
    return false;
}

static const char* sourceName(AudioTestSource source) {
    switch (source) {
        case AudioTestSource::FlashWav: return "Flash FS: /ESP32_onboard_ring.wav";
        case AudioTestSource::Tone: return "Generated tone";
        default: return "none";
    }
}

static const char* sfxPath(AudioSfxId id) {
    switch (id) {
        case AudioSfxId::BleEnabled: return SFX_BLE_ON;
        case AudioSfxId::BleDisabled: return SFX_BLE_OFF;
        case AudioSfxId::BleConnected: return SFX_BLE_CONNECTED;
        case AudioSfxId::BleDisconnected: return SFX_BLE_DISCONNECTED;
        case AudioSfxId::OperationOk: return SFX_OK;
        case AudioSfxId::OperationError: return SFX_ERROR;
        default: return nullptr;
    }
}

static void sfxToneFallback(AudioSfxId id, uint16_t& freq, uint16_t& duration) {
    switch (id) {
        case AudioSfxId::BleEnabled:      freq = 1000; duration = 120; break;
        case AudioSfxId::BleDisabled:     freq = 600;  duration = 120; break;
        case AudioSfxId::BleConnected:    freq = 1400; duration = 100; break;
        case AudioSfxId::BleDisconnected: freq = 500;  duration = 140; break;
        case AudioSfxId::OperationOk:     freq = 1200; duration = 90;  break;
        case AudioSfxId::OperationError:  freq = 350;  duration = 180; break;
        default:                          freq = 880;  duration = 120; break;
    }
}

static void applyCommand(const AudioCommand& cmd, ToneState& tone, PcmPlaybackState& pcm) {
    if ((cmd.type == AudioCommandType::PlayTestFallback || cmd.type == AudioCommandType::PlayTestTone || cmd.type == AudioCommandType::PlaySfx) && !g_i2sReady) {
        if (!initI2S()) {
            Serial.print("\n[AUDIO] I2S not ready, play command ignored");
            return;
        }
    }

    switch (cmd.type) {
        case AudioCommandType::PlayTestFallback: {
            tone.active = false;
            resetPcmState(pcm);

            LoadedPcmBuffer buffer;
            AudioTestSource src = AudioTestSource::None;
            if (selectFallbackSource(buffer, src)) {
                if (setI2SRate(buffer.sampleRate)) {
                    pcm.samples = buffer.samples;
                    pcm.framesTotal = buffer.frames;
                    pcm.framePos = 0;
                    pcm.sampleRate = buffer.sampleRate;
                    pcm.channels = buffer.channels;
                    pcm.source = src;
                    pcm.active = (pcm.framesTotal > 0 && pcm.samples != nullptr);
                    g_lastTestSource = src;
                    g_audioPlaybackActive = pcm.active;
                    Serial.printf("\n[AUDIO][TEST] source: %s", sourceName(src));
                } else {
                    if (buffer.samples != nullptr) {
                        free(buffer.samples);
                        buffer.samples = nullptr;
                    }
                    Serial.print("\n[AUDIO] Unable to set I2S sample rate for fallback source");
                }
            } else {
                // Fallback: если WAV из Flash FS недоступен, играем синтетический тон.
                g_lastTestSource = AudioTestSource::Tone;
                Serial.printf("\n[AUDIO][TEST] source: %s", sourceName(AudioTestSource::Tone));
                const AudioCommand toneCmd = { AudioCommandType::PlayTestTone, 880, 1200 };
                applyCommand(toneCmd, tone, pcm);
            }
            break;
        }
        case AudioCommandType::PlayTestTone: {
            resetPcmState(pcm);
            const uint16_t freq = (cmd.freqHz == 0) ? 880 : cmd.freqHz;
            const uint16_t duration = (cmd.durationMs == 0) ? 1000 : cmd.durationMs;

            tone.freqHz = freq;
            tone.remainingSamples = (static_cast<uint32_t>(AUDIO_SAMPLE_RATE) * duration) / 1000UL;
            tone.phase = 0;
            tone.active = (tone.remainingSamples > 0);
            g_lastTestSource = AudioTestSource::Tone;
            g_audioPlaybackActive = tone.active;
            (void)setI2SRate(AUDIO_SAMPLE_RATE);

            Serial.printf("\n[AUDIO] play test tone: %u Hz, %u ms", freq, duration);
            break;
        }
        case AudioCommandType::PlaySfx: {
            tone.active = false;
            resetPcmState(pcm);

            const AudioSfxId sfx = static_cast<AudioSfxId>(cmd.sfxId);
            const char* path = sfxPath(sfx);
            LoadedPcmBuffer buffer;

            if (path && loadWavFromFile(path, buffer) && setI2SRate(buffer.sampleRate)) {
                pcm.samples = buffer.samples;
                pcm.framesTotal = buffer.frames;
                pcm.framePos = 0;
                pcm.sampleRate = buffer.sampleRate;
                pcm.channels = buffer.channels;
                pcm.active = (pcm.framesTotal > 0 && pcm.samples != nullptr);
                g_audioPlaybackActive = pcm.active;
                Serial.printf("\n[AUDIO][SFX] source: Flash FS: %s", path);
            } else {
                if (buffer.samples != nullptr) {
                    free(buffer.samples);
                }
                uint16_t f = 880;
                uint16_t d = 120;
                sfxToneFallback(sfx, f, d);
                const AudioCommand toneCmd = { AudioCommandType::PlayTestTone, f, d, 0 };
                applyCommand(toneCmd, tone, pcm);
            }
            break;
        }
        case AudioCommandType::Stop:
            tone.active = false;
            tone.remainingSamples = 0;
            tone.phase = 0;
            resetPcmState(pcm);
            g_audioPlaybackActive = false;
            if (g_i2sReady) {
                i2s_zero_dma_buffer(AUDIO_I2S_PORT);
            }
            Serial.print("\n[AUDIO] stop playback");
            break;
    }
}

static void feedToneChunk(ToneState& tone) {
    if (!tone.active || tone.remainingSamples == 0 || !g_i2sReady) {
        return;
    }

    static int16_t interleaved[AUDIO_CHUNK_SAMPLES * 2];
    const uint32_t phaseStep = (static_cast<uint64_t>(tone.freqHz) * 0xFFFFFFFFULL) / AUDIO_SAMPLE_RATE;

    uint16_t count = AUDIO_CHUNK_SAMPLES;
    if (tone.remainingSamples < count) {
        count = static_cast<uint16_t>(tone.remainingSamples);
    }

    for (uint16_t i = 0; i < count; ++i) {
        tone.phase += phaseStep;
        const int16_t sample = (tone.phase & 0x80000000UL) ? 12000 : -12000;
        interleaved[2 * i] = sample;      // L
        interleaved[2 * i + 1] = sample;  // R
    }

    size_t bytesWritten = 0;
    const size_t bytesToWrite = static_cast<size_t>(count) * sizeof(int16_t) * 2;
    i2s_write(AUDIO_I2S_PORT, interleaved, bytesToWrite, &bytesWritten, pdMS_TO_TICKS(5));

    if (bytesWritten > 0) {
        const uint32_t samplesWritten = static_cast<uint32_t>(bytesWritten / (sizeof(int16_t) * 2));
        if (samplesWritten >= tone.remainingSamples) {
            tone.remainingSamples = 0;
        } else {
            tone.remainingSamples -= samplesWritten;
        }
    }

    if (tone.remainingSamples == 0) {
        tone.active = false;
        g_audioPlaybackActive = false;
        Serial.print("\n[AUDIO] Playback finished");
    }
}

static void feedPcmChunk(PcmPlaybackState& pcm) {
    if (!pcm.active || !pcm.samples || pcm.framePos >= pcm.framesTotal || !g_i2sReady) {
        return;
    }

    static int16_t interleaved[AUDIO_CHUNK_SAMPLES * 2];

    uint16_t frames = AUDIO_CHUNK_SAMPLES;
    const size_t remaining = pcm.framesTotal - pcm.framePos;
    if (remaining < frames) {
        frames = static_cast<uint16_t>(remaining);
    }

    if (pcm.channels == 1) {
        for (uint16_t i = 0; i < frames; ++i) {
            const int16_t s = pcm.samples[pcm.framePos + i];
            interleaved[2 * i] = s;
            interleaved[2 * i + 1] = s;
        }
    } else {
        const int16_t* src = pcm.samples + (pcm.framePos * 2);
        std::memcpy(interleaved, src, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
    }

    size_t bytesWritten = 0;
    const size_t bytesToWrite = static_cast<size_t>(frames) * 2 * sizeof(int16_t);
    i2s_write(AUDIO_I2S_PORT, interleaved, bytesToWrite, &bytesWritten, pdMS_TO_TICKS(5));

    if (bytesWritten > 0) {
        const size_t framesWritten = bytesWritten / (2 * sizeof(int16_t));
        pcm.framePos += framesWritten;
    }

    if (pcm.framePos >= pcm.framesTotal) {
        pcm.active = false;
        g_audioPlaybackActive = false;
        Serial.print("\n[AUDIO] Playback finished");
        resetPcmState(pcm);
    }
}

static void audioTaskEntry(void* /*param*/) {
    g_audioTaskRunning = true;
    Serial.print("\n[AUDIO] audioTask started (priority: LOW)");

    ToneState tone;
    PcmPlaybackState pcm;
    AudioCommand cmd;

    for (;;) {
        while (xQueueReceive(g_audioQueue, &cmd, 0) == pdTRUE) {
            applyCommand(cmd, tone, pcm);
        }

        if (!platformGetCapabilities().sound_enabled) {
            tone.active = false;
            resetPcmState(pcm);
            g_audioPlaybackActive = false;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        if (otaIsBusy()) {
            // Во время OTA аудио — наименьший приоритет.
            tone.active = false;
            resetPcmState(pcm);
            g_audioPlaybackActive = false;
            if (g_i2sReady) {
                i2s_zero_dma_buffer(AUDIO_I2S_PORT);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!g_i2sReady && !initI2S()) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        if (pcm.active) {
            feedPcmChunk(pcm);
            vTaskDelay(pdMS_TO_TICKS(1));
        } else if (tone.active) {
            feedToneChunk(tone);
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

} // namespace

void audioTaskStart() {
    if (g_audioTaskHandle != nullptr) {
        return;
    }

    if (g_audioQueue == nullptr) {
        g_audioQueue = xQueueCreate(8, sizeof(AudioCommand));
        if (g_audioQueue == nullptr) {
            Serial.print("\n[AUDIO] ERROR: failed to create command queue");
            return;
        }
    }

    // Core 1: рядом с loop(), но с минимальным приоритетом.
    BaseType_t result = xTaskCreatePinnedToCore(
        audioTaskEntry,
        "audio_task",
        6144,
        nullptr,
        AUDIO_TASK_PRIO,
        &g_audioTaskHandle,
        AUDIO_TASK_CORE
    );

    if (result != pdPASS) {
        g_audioTaskHandle = nullptr;
        g_audioTaskRunning = false;
        Serial.print("\n[AUDIO] ERROR: failed to create audioTask");
    }
}

bool audioTaskIsRunning() {
    return g_audioTaskRunning;
}

void audioTaskStop() {
    if (g_audioTaskHandle == nullptr) {
        return;
    }

    vTaskDelete(g_audioTaskHandle);
    g_audioTaskHandle = nullptr;
    g_audioTaskRunning = false;
    stopI2S();

    if (g_audioQueue != nullptr) {
        vQueueDelete(g_audioQueue);
        g_audioQueue = nullptr;
    }

    Serial.print("\n[AUDIO] audioTask stopped");
}

bool audioPlayTestTone(uint16_t frequencyHz, uint16_t durationMs) {
    if (g_audioQueue == nullptr) {
        return false;
    }

    AudioCommand cmd = {
        AudioCommandType::PlayTestTone,
        frequencyHz,
        durationMs,
        0
    };

    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

bool audioPlayTestFallback() {
    if (g_audioQueue == nullptr) {
        return false;
    }

    AudioCommand cmd = {
        AudioCommandType::PlayTestFallback,
        0,
        0,
        0
    };

    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

void audioStopPlayback() {
    if (g_audioQueue == nullptr) {
        return;
    }

    AudioCommand cmd = {
        AudioCommandType::Stop,
        0,
        0,
        0
    };

    (void)xQueueSend(g_audioQueue, &cmd, 0);
}

bool audioPlaySfx(AudioSfxId id) {
    if (g_audioQueue == nullptr) {
        return false;
    }

    AudioCommand cmd = {
        AudioCommandType::PlaySfx,
        0,
        0,
        static_cast<uint8_t>(id)
    };

    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

bool audioIsPlaying() {
    return g_audioPlaybackActive;
}

AudioTestSource audioGetLastTestSource() {
    return g_lastTestSource;
}

const char* audioTestSourceName(AudioTestSource source) {
    return sourceName(source);
}
