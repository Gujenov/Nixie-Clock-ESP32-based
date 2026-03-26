#include "audio_task.h"

#include "config.h"
#include "ota_manager.h"
#include "platform_profile.h"

#include <driver/i2s.h>
#include <SPIFFS.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include <cstring>
#include <cctype>

namespace {

constexpr i2s_port_t AUDIO_I2S_PORT = I2S_NUM_0;
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint16_t AUDIO_CHUNK_SAMPLES = 256;
constexpr uint8_t AUDIO_TASK_CORE = 1;
constexpr UBaseType_t AUDIO_TASK_PRIO = 1;
constexpr uint32_t SD_REPROBE_INTERVAL_MS = 60000UL;

enum class AudioCommandType : uint8_t {
    PlayFlashFile,
    PlaySdFile,
    PlayTestTone,
    PlaySfx,
    Stop
};

struct AudioCommand {
    AudioCommandType type;
    uint16_t freqHz;
    uint16_t durationMs;
    uint8_t sfxId;
    char path[96];
};

struct ToneState {
    bool active = false;
    uint16_t freqHz = 0;
    uint32_t remainingSamples = 0;
    uint32_t phase = 0;
};

struct WavStreamState {
    bool active = false;
    File file;
    size_t dataBytesRemaining = 0;
    uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    uint8_t channels = 1; // 1 = mono, 2 = stereo
    bool isSdStream = false;
    AudioTestSource source = AudioTestSource::None;
};

static constexpr char FLASH_ALARM_WAV[] = "/alarm_default.wav";
static constexpr char FLASH_CHIMES_WAV[] = "/sfx_himes.wav";
static constexpr char SD_CHIME_HOUR_WAV[] = "/chime_hour.wav";
static constexpr char SD_CHIME_QUARTER_WAV[] = "/chime_quarter.wav";
static constexpr char SFX_BLE_ON[] = "/sfx_ble_on.wav";
static constexpr char SFX_BLE_OFF[] = "/sfx_ble_off.wav";
static constexpr char SFX_BLE_CONNECTED[] = "/sfx_ble_connected.wav";
static constexpr char SFX_BLE_DISCONNECTED[] = "/sfx_ble_disconnected.wav";
static constexpr char SFX_OK[] = "/sfx_ok.wav";
static constexpr char SFX_ERROR[] = "/sfx_error.wav";

static TaskHandle_t g_audioTaskHandle = nullptr;
static volatile bool g_audioTaskRunning = false;
static volatile bool g_audioPlaybackActive = false;
static QueueHandle_t g_audioQueue = nullptr;
static bool g_i2sReady = false;
static bool g_flashFsReady = false;
static bool g_sdReady = false;
static uint32_t g_lastSdProbeMs = 0;
static AudioTestSource g_lastTestSource = AudioTestSource::None;
static SPIClass g_sdSpi(FSPI);

static void invalidateSdMount(const char* reason = nullptr) {
    if (reason && reason[0] != '\0') {
        Serial.printf("\n[AUDIO][SD] %s", reason);
    }
    g_sdReady = false;
    SD.end();
    g_lastSdProbeMs = millis();
}

static void resetWavStreamState(WavStreamState& wav) {
    if (wav.file) {
        wav.file.close();
    }
    wav.active = false;
    wav.dataBytesRemaining = 0;
    wav.sampleRate = AUDIO_SAMPLE_RATE;
    wav.channels = 1;
    wav.isSdStream = false;
    wav.source = AudioTestSource::None;
}

static bool ensureFlashFsMounted() {
    if (g_flashFsReady) {
        return true;
    }

    if (!SPIFFS.begin(false)) {
        Serial.print("\n[AUDIO] SPIFFS mount failed, пробую форматировать раздел...");
        if (!SPIFFS.begin(true)) {
            Serial.print("\n[AUDIO] SPIFFS format+mount failed (проверьте таблицу разделов и загрузку FS-образа)");
            return false;
        }
        Serial.print("\n[AUDIO] SPIFFS был повреждён/пустой и отформатирован");
    }

    g_flashFsReady = true;
    return true;
}

static bool ensureSdMounted(bool forceProbe = false) {
    if (g_sdReady) {
        if (SD.cardType() == CARD_NONE) {
            invalidateSdMount("card removed");
            return false;
        }
        return true;
    }

    if (!forceProbe && g_lastSdProbeMs != 0) {
        const uint32_t elapsed = millis() - g_lastSdProbeMs;
        if (elapsed < SD_REPROBE_INTERVAL_MS) {
            return false;
        }
    }

    g_lastSdProbeMs = millis();

    g_sdSpi.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, g_sdSpi)) {
        SD.end();
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        invalidateSdMount("card detect failed after mount");
        return false;
    }

    g_sdReady = true;
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
    Serial.printf("\n[AUDIO] I2S шина готова");
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

static bool openWavStreamFromFs(fs::FS& fs, const char* path, WavStreamState& outStream) {
    if (!path || path[0] == '\0') {
        return false;
    }

    File f = fs.open(path, FILE_READ);
    if (!f) {
        Serial.printf("\n[AUDIO] WAV open failed: %s", path);
        return false;
    }

    const size_t fileSize = static_cast<size_t>(f.size());
    if (fileSize < 44) {
        f.close();
        Serial.printf("\n[AUDIO] WAV too small: %s (%lu bytes)", path, static_cast<unsigned long>(fileSize));
        return false;
    }

    uint8_t header[44] = {0};
    if (f.read(header, sizeof(header)) != sizeof(header)) {
        f.close();
        Serial.printf("\n[AUDIO] WAV header read failed: %s", path);
        return false;
    }

    const bool riffOk = std::memcmp(header + 0, "RIFF", 4) == 0;
    const bool waveOk = std::memcmp(header + 8, "WAVE", 4) == 0;
    const bool fmtOk = std::memcmp(header + 12, "fmt ", 4) == 0;
    const bool dataOk = std::memcmp(header + 36, "data", 4) == 0;
    if (!riffOk || !waveOk || !fmtOk || !dataOk) {
        f.close();
        Serial.printf("\n[AUDIO] WAV header unsupported: %s", path);
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
        Serial.printf("\n[AUDIO] WAV format must be PCM16 mono/stereo: %s", path);
        return false;
    }

    if ((44U + dataSize) > fileSize) {
        f.close();
        Serial.printf("\n[AUDIO] WAV data chunk inconsistent: %s (data=%lu, file=%lu)",
                      path,
                      static_cast<unsigned long>(dataSize),
                      static_cast<unsigned long>(fileSize));
        return false;
    }

    outStream.file = f;
    outStream.channels = static_cast<uint8_t>(channels);
    outStream.sampleRate = sampleRate;
    outStream.dataBytesRemaining = dataSize;
    outStream.active = (dataSize > 0);
    return true;
}

static bool pathHasWavExtension(const char* name) {
    if (!name) return false;
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return (tolower(static_cast<unsigned char>(dot[1])) == 'w' &&
            tolower(static_cast<unsigned char>(dot[2])) == 'a' &&
            tolower(static_cast<unsigned char>(dot[3])) == 'v' &&
            dot[4] == '\0');
}

static bool findSdTestWav(char* outPath, size_t outPathSize) {
    if (!ensureSdMounted()) {
        return false;
    }

    static const char* kAudioDirs[] = {"/alarms", "/Alarms", "/bells", "/Bells"};
    for (size_t d = 0; d < (sizeof(kAudioDirs) / sizeof(kAudioDirs[0])); ++d) {
        File folder = SD.open(kAudioDirs[d]);
        if (!folder || !folder.isDirectory()) {
            continue;
        }

        File f = folder.openNextFile();
        while (f) {
            if (!f.isDirectory() && pathHasWavExtension(f.name())) {
                strlcpy(outPath, f.name(), outPathSize);
                f.close();
                folder.close();
                return true;
            }
            f.close();
            f = folder.openNextFile();
        }

        folder.close();
    }

    return false;
}

static bool findSdAlarmWavByMelody(uint8_t melodyNumber, char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0) return false;
    if (!ensureSdMounted()) return false;

    // Приоритетные имена в каталогах /alarms и /Alarms:
    // alarm_<N>.wav, alarm<N>.wav, <N>.wav
    static const char* kAlarmDirs[] = {"/alarms", "/Alarms"};
    char candidates[6][48] = {{0}};
    size_t candidateCount = 0;

    for (size_t d = 0; d < (sizeof(kAlarmDirs) / sizeof(kAlarmDirs[0])); ++d) {
        const char* dir = kAlarmDirs[d];
        snprintf(candidates[candidateCount++], sizeof(candidates[0]), "%s/alarm_%u.wav", dir, static_cast<unsigned>(melodyNumber));
        snprintf(candidates[candidateCount++], sizeof(candidates[0]), "%s/alarm%u.wav", dir, static_cast<unsigned>(melodyNumber));
        snprintf(candidates[candidateCount++], sizeof(candidates[0]), "%s/%u.wav", dir, static_cast<unsigned>(melodyNumber));
    }

    for (size_t i = 0; i < candidateCount; ++i) {
        if (SD.exists(candidates[i])) {
            strlcpy(outPath, candidates[i], outPathSize);
            return true;
        }
    }

    for (size_t d = 0; d < (sizeof(kAlarmDirs) / sizeof(kAlarmDirs[0])); ++d) {
        const char* dir = kAlarmDirs[d];
        File folder = SD.open(dir);
        if (!folder || !folder.isDirectory()) {
            continue;
        }

        File f = folder.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                const char* name = f.name();
                if (name && pathHasWavExtension(name)) {
                    // Допускаем форматы: alarm_3.wav / alarm3.wav / 3.wav
                    int parsed = -1;
                    if (sscanf(name, "%*[^0-9]%d.wav", &parsed) == 1 ||
                        sscanf(name, "%d.wav", &parsed) == 1) {
                        if (parsed == static_cast<int>(melodyNumber)) {
                            strlcpy(outPath, name, outPathSize);
                            f.close();
                            folder.close();
                            return true;
                        }
                    }
                }
            }
            f.close();
            f = folder.openNextFile();
        }

        folder.close();
    }

    return false;
}

static bool resolveSdChimePath(const char* fileName, char* outPath, size_t outPathSize) {
    if (!fileName || fileName[0] == '\0' || !outPath || outPathSize == 0) {
        return false;
    }
    if (!ensureSdMounted()) {
        return false;
    }

    static const char* kChimeDirs[] = {"/bells", "/Bells"};
    char candidate[64] = {0};

    for (size_t i = 0; i < (sizeof(kChimeDirs) / sizeof(kChimeDirs[0])); ++i) {
        const char* dir = kChimeDirs[i];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, fileName);

        if (SD.exists(candidate)) {
            strlcpy(outPath, candidate, outPathSize);
            return true;
        }
    }

    return false;
}

static const char* selectFlashTestFile() {
    if (SPIFFS.exists(FLASH_ALARM_WAV)) {
        return FLASH_ALARM_WAV;
    }
    return nullptr;
}

static void probeAudioSourcesOnStartup() {
    const bool sdMounted = ensureSdMounted(true);
    Serial.print(sdMounted ? "\n[AUDIO] microSD найдена" : "\n[AUDIO] microSD не найдена");

    if (sdMounted) {
        return;
    }

    const bool flashReady = ensureFlashFsMounted();
    const char* selectedFlashFile = flashReady ? selectFlashTestFile() : nullptr;
    if (selectedFlashFile != nullptr) {
        Serial.printf("\n[AUDIO] Внутренний WAV найден: %s", selectedFlashFile);
    } else {
        Serial.print("\n[AUDIO] Внутренний WAV-файл не найден, доступен тональный сигнал");
    }
}

static const char* sourceName(AudioTestSource source) {
    switch (source) {
        case AudioTestSource::FlashWav: return "Flash FS: internal WAV";
        case AudioTestSource::Tone: return "Generated tone";
        default: return "none";
    }
}

static const char* startStatusNameInternal(AudioStartStatus status) {
    switch (status) {
        case AudioStartStatus::Queued: return "queued";
        case AudioStartStatus::ErrorQueueUnavailable: return "queue unavailable";
        case AudioStartStatus::ErrorFlashFsUnavailable: return "Flash FS unavailable";
        case AudioStartStatus::ErrorFlashFileNotFound: return "Flash file not found";
        case AudioStartStatus::ErrorSdCardUnavailable: return "microSD unavailable";
        case AudioStartStatus::ErrorSdAudioNotFound: return "microSD audio not found";
        default: return "unknown";
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

static void applyCommand(const AudioCommand& cmd, ToneState& tone, WavStreamState& wav) {
    if ((cmd.type == AudioCommandType::PlayFlashFile || cmd.type == AudioCommandType::PlaySdFile || cmd.type == AudioCommandType::PlayTestTone || cmd.type == AudioCommandType::PlaySfx) && !g_i2sReady) {
        if (!initI2S()) {
            Serial.print("\n[AUDIO] I2S not ready, play command ignored");
            return;
        }
    }

    switch (cmd.type) {
        case AudioCommandType::PlayFlashFile: {
            tone.active = false;
            resetWavStreamState(wav);

            if (ensureFlashFsMounted() && openWavStreamFromFs(SPIFFS, cmd.path, wav)) {
                wav.isSdStream = false;
                if (setI2SRate(wav.sampleRate)) {
                    wav.source = AudioTestSource::FlashWav;
                    g_lastTestSource = AudioTestSource::FlashWav;
                    g_audioPlaybackActive = wav.active;
                    Serial.printf("\n[AUDIO][TEST] source: Flash FS: %s", cmd.path);
                } else {
                    resetWavStreamState(wav);
                    Serial.print("\n[AUDIO] Unable to set I2S sample rate for fallback source");
                }
            } else {
                Serial.printf("\n[AUDIO] Ошибка: не удалось прочитать файл из Flash FS: %s (подробности выше)", cmd.path);
            }
            break;
        }
        case AudioCommandType::PlaySdFile: {
            tone.active = false;
            resetWavStreamState(wav);

            if (ensureSdMounted() && openWavStreamFromFs(SD, cmd.path, wav)) {
                wav.isSdStream = true;
                if (setI2SRate(wav.sampleRate)) {
                    wav.source = AudioTestSource::FlashWav;
                    g_lastTestSource = AudioTestSource::FlashWav;
                    g_audioPlaybackActive = wav.active;
                    Serial.printf("\n[AUDIO][TEST] source: microSD: %s", cmd.path);
                } else {
                    resetWavStreamState(wav);
                    Serial.print("\n[AUDIO] Ошибка: не удалось выставить I2S частоту для microSD файла");
                }
            } else {
                Serial.printf("\n[AUDIO] Ошибка: не удалось прочитать WAV с microSD: %s (подробности выше)", cmd.path);
            }
            break;
        }
        case AudioCommandType::PlayTestTone: {
            resetWavStreamState(wav);
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
            resetWavStreamState(wav);

            const AudioSfxId sfx = static_cast<AudioSfxId>(cmd.sfxId);
            const char* path = sfxPath(sfx);

            if (path && ensureFlashFsMounted() && openWavStreamFromFs(SPIFFS, path, wav) && setI2SRate(wav.sampleRate)) {
                wav.isSdStream = false;
                wav.source = AudioTestSource::FlashWav;
                g_audioPlaybackActive = wav.active;
                Serial.printf("\n[AUDIO][SFX] source: Flash FS: %s", path);
            } else {
                resetWavStreamState(wav);
                uint16_t f = 880;
                uint16_t d = 120;
                sfxToneFallback(sfx, f, d);
                const AudioCommand toneCmd = { AudioCommandType::PlayTestTone, f, d, 0, {0} };
                applyCommand(toneCmd, tone, wav);
            }
            break;
        }
        case AudioCommandType::Stop:
            tone.active = false;
            tone.remainingSamples = 0;
            tone.phase = 0;
            resetWavStreamState(wav);
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

static void feedWavStreamChunk(WavStreamState& wav) {
    if (!wav.active || !wav.file || wav.dataBytesRemaining == 0 || !g_i2sReady) {
        return;
    }

    static int16_t interleaved[AUDIO_CHUNK_SAMPLES * 2];
    static int16_t monoBuffer[AUDIO_CHUNK_SAMPLES];

    uint16_t frames = AUDIO_CHUNK_SAMPLES;
    const size_t frameBytes = static_cast<size_t>(wav.channels) * sizeof(int16_t);
    const size_t remainingFrames = wav.dataBytesRemaining / frameBytes;
    if (remainingFrames < frames) {
        frames = static_cast<uint16_t>(remainingFrames);
    }

    if (frames == 0) {
        wav.active = false;
        g_audioPlaybackActive = false;
        resetWavStreamState(wav);
        Serial.print("\n[AUDIO] Playback finished");
        return;
    }

    size_t payloadBytes = 0;

    if (wav.channels == 1) {
        payloadBytes = static_cast<size_t>(frames) * sizeof(int16_t);
        const size_t readBytes = wav.file.read(reinterpret_cast<uint8_t*>(monoBuffer), payloadBytes);
        if (readBytes == 0) {
            if (wav.isSdStream) {
                invalidateSdMount("read error while streaming (mono)");
            }
            wav.active = false;
            g_audioPlaybackActive = false;
            resetWavStreamState(wav);
            Serial.print("\n[AUDIO] WAV stream read reached EOF unexpectedly");
            Serial.print("\n[AUDIO] Playback finished");
            return;
        }

        const uint16_t framesRead = static_cast<uint16_t>(readBytes / sizeof(int16_t));
        for (uint16_t i = 0; i < framesRead; ++i) {
            const int16_t s = monoBuffer[i];
            interleaved[2 * i] = s;
            interleaved[2 * i + 1] = s;
        }
        frames = framesRead;
        payloadBytes = static_cast<size_t>(frames) * sizeof(int16_t);
    } else {
        payloadBytes = static_cast<size_t>(frames) * 2 * sizeof(int16_t);
        const size_t readBytes = wav.file.read(reinterpret_cast<uint8_t*>(interleaved), payloadBytes);
        if (readBytes == 0) {
            if (wav.isSdStream) {
                invalidateSdMount("read error while streaming (stereo)");
            }
            wav.active = false;
            g_audioPlaybackActive = false;
            resetWavStreamState(wav);
            Serial.print("\n[AUDIO] WAV stream read reached EOF unexpectedly");
            Serial.print("\n[AUDIO] Playback finished");
            return;
        }
        frames = static_cast<uint16_t>(readBytes / (2 * sizeof(int16_t)));
        payloadBytes = static_cast<size_t>(frames) * 2 * sizeof(int16_t);
    }

    if (frames == 0) {
        wav.active = false;
        g_audioPlaybackActive = false;
        resetWavStreamState(wav);
        Serial.print("\n[AUDIO] Playback finished");
        return;
    }

    const size_t bytesToWrite = static_cast<size_t>(frames) * 2 * sizeof(int16_t);
    size_t totalWritten = 0;
    const uint8_t* outPtr = reinterpret_cast<const uint8_t*>(interleaved);
    const unsigned long writeDeadline = millis() + 40UL;

    while (totalWritten < bytesToWrite) {
        size_t chunkWritten = 0;
        i2s_write(AUDIO_I2S_PORT,
                  outPtr + totalWritten,
                  bytesToWrite - totalWritten,
                  &chunkWritten,
                  pdMS_TO_TICKS(5));

        totalWritten += chunkWritten;

        if (chunkWritten == 0) {
            if (millis() >= writeDeadline) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // ВАЖНО: из файла уже считан payloadBytes, поэтому счётчик оставшихся данных
    // должен уменьшаться именно на объём прочитанного файла, а не на bytesWritten в I2S.
    // Иначе при частичных i2s_write возникает ложный "interrupted before data chunk end".
    const size_t consumedBytes = payloadBytes;
    if (consumedBytes >= wav.dataBytesRemaining) {
        wav.dataBytesRemaining = 0;
    } else {
        wav.dataBytesRemaining -= consumedBytes;
    }

    if (totalWritten < bytesToWrite) {
        Serial.printf("\n[AUDIO] WARN: I2S partial write %lu/%lu bytes",
                      static_cast<unsigned long>(totalWritten),
                      static_cast<unsigned long>(bytesToWrite));
    }

    if (wav.dataBytesRemaining == 0) {
        wav.active = false;
        g_audioPlaybackActive = false;
        Serial.print("\n[AUDIO] Playback finished");
        resetWavStreamState(wav);
        return;
    }

    if (!wav.file.available()) {
        if (wav.isSdStream) {
            invalidateSdMount("stream ended unexpectedly (card removed?)");
        }
        wav.active = false;
        g_audioPlaybackActive = false;
        Serial.println("\n[AUDIO] WAV stream interrupted before data chunk end");
        Serial.println("\n[AUDIO] Playback finished");
        resetWavStreamState(wav);
    }
}

static void audioTaskEntry(void* /*param*/) {
    g_audioTaskRunning = true;

    const bool i2sReadyNow = initI2S();
    if (i2sReadyNow) {
        Serial.print("\n[AUDIO] Инициализировано");
    } else {
        Serial.print("\n[AUDIO] Инициализировано (I2S недоступна, повторная инициализация в фоне)");
    }

    probeAudioSourcesOnStartup();

    ToneState tone;
    WavStreamState wav;
    AudioCommand cmd;

    for (;;) {
        while (xQueueReceive(g_audioQueue, &cmd, 0) == pdTRUE) {
            applyCommand(cmd, tone, wav);
        }

        if (!platformGetCapabilities().sound_enabled) {
            tone.active = false;
            resetWavStreamState(wav);
            g_audioPlaybackActive = false;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        if (otaIsBusy()) {
            // Во время OTA аудио — наименьший приоритет.
            tone.active = false;
            resetWavStreamState(wav);
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

        if (wav.active) {
            feedWavStreamChunk(wav);
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

    if (!platformGetCapabilities().sound_enabled) {
        Serial.print("\n[AUDIO] Подсистема отключена, запуск audioTask пропущен");
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
        0,
        {0}
    };

    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

bool audioPlayTestFallback() {
    return audioPlayFromFlashTest() == AudioStartStatus::Queued;
}

void audioStopPlayback() {
    if (g_audioQueue == nullptr) {
        return;
    }

    AudioCommand cmd = {
        AudioCommandType::Stop,
        0,
        0,
        0,
        {0}
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
        static_cast<uint8_t>(id),
        {0}
    };

    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

AudioStartStatus audioPlayFromFlashTest() {
    if (g_audioQueue == nullptr) {
        return AudioStartStatus::ErrorQueueUnavailable;
    }
    if (!ensureFlashFsMounted()) {
        return AudioStartStatus::ErrorFlashFsUnavailable;
    }
    const char* selectedFlashFile = selectFlashTestFile();
    if (selectedFlashFile == nullptr) {
        return AudioStartStatus::ErrorFlashFileNotFound;
    }

    AudioCommand cmd = {
        AudioCommandType::PlayFlashFile,
        0,
        0,
        0,
        {0}
    };
    strlcpy(cmd.path, selectedFlashFile, sizeof(cmd.path));
    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE ? AudioStartStatus::Queued : AudioStartStatus::ErrorQueueUnavailable;
}

AudioStartStatus audioPlayFromSdTest() {
    if (g_audioQueue == nullptr) {
        return AudioStartStatus::ErrorQueueUnavailable;
    }
    if (!ensureSdMounted(true)) {
        return AudioStartStatus::ErrorSdCardUnavailable;
    }

    char foundPath[96] = {0};
    if (!findSdTestWav(foundPath, sizeof(foundPath))) {
        return AudioStartStatus::ErrorSdAudioNotFound;
    }

    AudioCommand cmd = {
        AudioCommandType::PlaySdFile,
        0,
        0,
        0,
        {0}
    };
    strlcpy(cmd.path, foundPath, sizeof(cmd.path));
    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE ? AudioStartStatus::Queued : AudioStartStatus::ErrorQueueUnavailable;
}

bool audioPlayAlarmMelody(uint8_t melodyNumber) {
    if (g_audioQueue == nullptr) {
        return false;
    }

    if (melodyNumber == 0) {
        melodyNumber = 1;
    }

    char foundPath[96] = {0};
    if (findSdAlarmWavByMelody(melodyNumber, foundPath, sizeof(foundPath))) {
        AudioCommand cmd = {
            AudioCommandType::PlaySdFile,
            0,
            0,
            0,
            {0}
        };
        strlcpy(cmd.path, foundPath, sizeof(cmd.path));
        if (xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE) {
            Serial.printf("\n[ALARM] Воспроизведение с microSD: %s", foundPath);
            return true;
        }
    }

    AudioStartStatus flashStatus = audioPlayFromFlashTest();
    if (flashStatus == AudioStartStatus::Queued) {
        Serial.print("\n[ALARM] microSD трек не найден, fallback: /alarm_default.wav из Flash FS");
        return true;
    }

    if (audioPlayTestTone(880, 1200)) {
        Serial.print("\n[ALARM] WAV не найден, fallback: тональный сигнал");
        return true;
    }

    Serial.print("\n[ALARM] Не удалось запустить сигнал будильника");
    return false;
}

static bool queuePlaySdFileByPath(const char* path) {
    if (g_audioQueue == nullptr || !path || path[0] == '\0') {
        return false;
    }

    AudioCommand cmd = {
        AudioCommandType::PlaySdFile,
        0,
        0,
        0,
        {0}
    };
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

static bool queuePlayFlashFileByPath(const char* path) {
    if (g_audioQueue == nullptr || !path || path[0] == '\0') {
        return false;
    }

    AudioCommand cmd = {
        AudioCommandType::PlayFlashFile,
        0,
        0,
        0,
        {0}
    };
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return xQueueSend(g_audioQueue, &cmd, 0) == pdTRUE;
}

static bool playChimeByPathWithFallback(const char* sdPath, const char* chimeLabel) {
    if (g_audioQueue == nullptr) {
        Serial.printf("\n[AUDIO][BELL] %s: очередь недоступна", chimeLabel);
        return false;
    }

    char resolvedSdPath[96] = {0};
    const char* baseName = (sdPath && sdPath[0] == '/') ? (sdPath + 1) : sdPath;
    if (resolveSdChimePath(baseName, resolvedSdPath, sizeof(resolvedSdPath))) {
        if (queuePlaySdFileByPath(resolvedSdPath)) {
            Serial.printf("\n[AUDIO][BELL] %s: microSD %s", chimeLabel, resolvedSdPath);
            return true;
        }
        Serial.printf("\n[AUDIO][BELL] %s: не удалось поставить в очередь microSD %s", chimeLabel, resolvedSdPath);
    }

    if (ensureFlashFsMounted() && SPIFFS.exists(FLASH_CHIMES_WAV)) {
        if (queuePlayFlashFileByPath(FLASH_CHIMES_WAV)) {
            Serial.printf("\n[AUDIO][BELL] %s: fallback Flash %s", chimeLabel, FLASH_CHIMES_WAV);
            return true;
        }
        Serial.printf("\n[AUDIO][BELL] %s: не удалось поставить fallback %s", chimeLabel, FLASH_CHIMES_WAV);
        return false;
    }

    Serial.printf("\n[AUDIO][BELL] %s: файл не найден (microSD/Flash), пропуск", chimeLabel);
    return false;
}

bool audioPlayChimeHourly() {
    return playChimeByPathWithFallback(SD_CHIME_HOUR_WAV, "hourly");
}

bool audioPlayChimeQuarter() {
    return playChimeByPathWithFallback(SD_CHIME_QUARTER_WAV, "quarter");
}

const char* audioStartStatusName(AudioStartStatus status) {
    return startStatusNameInternal(status);
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
