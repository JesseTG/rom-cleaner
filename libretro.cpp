// MIT-licensed, see LICENSE in the root directory.

#include <array>
#include <cstddef>
#include <cstring>
#include <kiss_fft.h>
#include <memory>

#include <battery/embed.hpp>
#include <libretro.h>
#include <pntr.h>
#include <retro_assert.h>
#include <span>
#include <audio/audio_mixer.h>
#include <audio/conversion/float_to_s16.h>
#include <file/file_path.h>
#include <string/stdstring.h>

#include "constants.hpp"
using std::array;


static constexpr int RMS_THRESHOLD = 100;  // Further lowered threshold
static constexpr float BLOW_RATIO = 0.55f; // More lenient ratio
static constexpr int SMOOTHING_FRAMES = 6;
static constexpr int LOW_FREQ_LIMIT = 600;  // Expanded range
static constexpr int ADAPTIVE_WINDOW = 30;  // For background noise estimation

namespace
{
    retro_video_refresh_t _video_refresh = nullptr;
    retro_audio_sample_t _audio_sample = nullptr;
    retro_audio_sample_batch_t _audio_sample_batch = nullptr;
    retro_input_poll_t _input_poll = nullptr;
    retro_input_state_t _input_state = nullptr;
    retro_environment_t _environment = nullptr;
    retro_log_printf_t _log = nullptr;
}

struct CoreState
{
    CoreState() noexcept
    {
        _framebuffer = pntr_new_image(SCREEN_WIDTH, SCREEN_HEIGHT);
        retro_assert(_framebuffer != nullptr);

        _fftConfig = kiss_fft_alloc(SAMPLES_PER_FRAME, 0, nullptr, nullptr);
    }

    ~CoreState() noexcept
    {
        kiss_fft_free(_fftConfig);

        pntr_unload_image(_framebuffer);
        _framebuffer = nullptr;

        if (_microphone) {
            _microphoneInterface.set_mic_state(_microphone, false);
            _microphoneInterface.close_mic(_microphone);
        }
    }

    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    bool LoadGame(const retro_game_info& game);
    void Run();

    const bool initialized = true;
private:
    audio_mixer_sound_t* _fanfareSound = nullptr;
    audio_mixer_voice_t* _fanfareVoice = nullptr;
    retro_microphone_interface _microphoneInterface;
    retro_microphone* _microphone = nullptr;
    retro_microphone_params_t _actualMicParams {};
    double _dustiness = 100.0;
    bool _micInitialized = false;
    pntr_image* _framebuffer = nullptr;
    kiss_fft_cfg _fftConfig = nullptr;
    double _adaptiveThreshold = RMS_THRESHOLD;
    size_t _historyIndex = 0;
    std::array<bool, SMOOTHING_FRAMES> _detectionHistory = {};
    std::array<double, ADAPTIVE_WINDOW> _backgroundLevels = {};
    size_t _bgIndex = 0;
    bool InitMicrophone();
    bool IsBlowing(std::span<const int16_t> samples);
};

namespace {
    alignas(CoreState) std::array<uint8_t, sizeof(CoreState)> CoreStateBuffer;
    CoreState& Core = *reinterpret_cast<CoreState*>(CoreStateBuffer.data());
}


RETRO_API void retro_set_video_refresh(retro_video_refresh_t refresh)
{
    _video_refresh = refresh;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t audio_sample)
{
    _audio_sample = audio_sample;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t audio_sample_batch)
{
    _audio_sample_batch = audio_sample_batch;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t input_poll)
{
    _input_poll = input_poll;
}

RETRO_API void retro_set_input_state(retro_input_state_t input_state)
{
    _input_state = input_state;
}

RETRO_API void retro_set_environment(retro_environment_t env)
{
    _environment = env;
    retro_log_callback log = { .log = nullptr };
    retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    _environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log);
    _environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);

    if (!_log && log.log)
    {
        _log = log.log;
        _log(RETRO_LOG_DEBUG, "Loggin' in the air\n");
    }
}


RETRO_API void retro_init()
{
    CoreStateBuffer.fill({});
    new(&CoreStateBuffer) CoreState(); // placement-new the CoreState
    retro_assert(Core.initialized);
}


RETRO_API void retro_deinit()
{
    Core.~CoreState(); // placement delete
    CoreStateBuffer.fill({});
    retro_assert(!Core.initialized);
}


RETRO_API unsigned retro_api_version()
{
    return RETRO_API_VERSION;
}


RETRO_API void retro_get_system_info(retro_system_info *info)
{
    info->library_name = "ROM Cleaner";
    info->block_extract = false;
    info->library_version = "0.0.0";
    info->valid_extensions = "smc|sfc";
}


RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = SCREEN_WIDTH;
    info->geometry.base_height = SCREEN_HEIGHT;
    info->geometry.max_width = SCREEN_WIDTH;
    info->geometry.max_height = SCREEN_HEIGHT;
    info->timing.fps = FPS;
    info->timing.sample_rate = SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void)
{
}

RETRO_API size_t retro_serialize_size(void)
{
    return 0;
}

/* Serializes internal state. If failed, or size is lower than
 * retro_serialize_size(), it should return false, true otherwise. */
RETRO_API bool retro_serialize(void *data, size_t size) { return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { return false; }

RETRO_API void retro_cheat_reset() {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}

/* Loads a game.
 * Return true to indicate successful loading and false to indicate load failure.
 */
RETRO_API bool retro_load_game(const struct retro_game_info *game) try
{
    if (game == nullptr) {
        _log(RETRO_LOG_ERROR, "No game provided\n");
        return false;
    }

    return Core.LoadGame(*game);
}
catch (const std::exception &e) {
    retro_message_ext error {
        .msg = e.what(),
        .duration = 3000,
        .level = RETRO_LOG_ERROR,
        .target = RETRO_MESSAGE_TARGET_ALL,
        .type = RETRO_MESSAGE_TYPE_NOTIFICATION,
    };

    _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &error);
    return false;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *info, size_t)
{
    return retro_load_game(info);
}

/* Unloads the currently loaded game. Called before retro_deinit(void). */
RETRO_API void retro_unload_game()
{
}

RETRO_API unsigned retro_get_region() { return RETRO_REGION_NTSC; }

/* Gets region of memory. */
RETRO_API void *retro_get_memory_data(unsigned id)
{
    return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
    return 0;
}

RETRO_API void retro_run()
{
    Core.Run();
}

bool CoreState::IsBlowing(std::span<const int16_t> samples) {

    // Compute RMS Energy
    double rms = 0.0;
    for (int16_t sample : samples) {
        rms += static_cast<double>(sample) * sample;
    }
    rms = sqrt(rms / samples.size());

    // Update adaptive background level
    _backgroundLevels[_bgIndex] = rms;
    _bgIndex = (_bgIndex + 1) % ADAPTIVE_WINDOW;

    // Calculate adaptive threshold
    double avgBgNoise = 0.0;
    for (double level : _backgroundLevels) {
        avgBgNoise += level;
    }
    avgBgNoise /= ADAPTIVE_WINDOW;
    _adaptiveThreshold = std::max<double>(RMS_THRESHOLD, avgBgNoise * 1.5);

    // Early exit if too quiet
    if (rms < _adaptiveThreshold) {
        _detectionHistory[_historyIndex] = false;
        _historyIndex = (_historyIndex + 1) % SMOOTHING_FRAMES;
        return false;
    }

    // Convert samples for FFT (with windowing for better spectral resolution)
    std::array<kiss_fft_cpx, SAMPLES_PER_FRAME> in, out;
    for (size_t i = 0; i < samples.size(); i++) {
        // Apply Hann window for better frequency resolution
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (samples.size() - 1)));
        in[i].r = static_cast<float>(samples[i]) / 32768.0f * window;
        in[i].i = 0;
    }

    // Execute FFT
    kiss_fft(_fftConfig, in.data(), out.data());

    // Analyze frequency content
    double bin_size = SAMPLE_RATE / static_cast<double>(samples.size());
    double low_freq_energy = 0.0, total_energy = 0.0;

    // Look for blow signature (characteristic hump around 200-400Hz)
    double blow_signature_energy = 0.0;
    double signature_peak = 0.0;

    // Skip DC component (i=0)
    for (int i = 1; i < samples.size() / 2; i++) {
        double freq = i * bin_size;
        double magnitude = sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
        total_energy += magnitude;

        if (freq < LOW_FREQ_LIMIT) {
            low_freq_energy += magnitude;

            // Look for blow signature (focused energy in 150-450Hz range)
            if (freq > 150 && freq < 450) {
                blow_signature_energy += magnitude;
                signature_peak = std::max(signature_peak, magnitude);
            }
        }
    }

    // Current frame detection (multiple criteria)
    bool frequencyRatio = (total_energy > 0) && ((low_freq_energy / total_energy) > BLOW_RATIO);
    bool signatureStrength = (total_energy > 0) && ((blow_signature_energy / total_energy) > 0.3);
    bool signaturePeak = signature_peak > (total_energy / samples.size() * 3.0);

    bool currentDetection = frequencyRatio && (signatureStrength || signaturePeak);

    // Update history
    _detectionHistory[_historyIndex] = currentDetection;
    _historyIndex = (_historyIndex + 1) % SMOOTHING_FRAMES;

    // Count positive detections in history
    int positiveCount = 0;
    for (bool detection : _detectionHistory) {
        if (detection) positiveCount++;
    }

    // Return true if a significant portion of frames detect blowing
    return positiveCount >= (SMOOTHING_FRAMES / 3.0);  // More lenient (1/3 instead of 1/2)
}
bool CoreState::LoadGame(const retro_game_info& game) {
    if (string_is_empty(game.path)) {
        throw std::runtime_error("No game path provided");
    }

    _microphoneInterface.interface_version = RETRO_MICROPHONE_INTERFACE_VERSION;
    if (!_environment(RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE, &_microphoneInterface)) {
        throw std::runtime_error("Failed to get microphone interface");
    }

    //_micInitialized = InitMicrophone();

    std::string_view extension = path_get_extension(game.path);

    if (extension == "sfc" || extension == "smc") {
        // TODO: Load the SNES cart image
    }

    return true;
}


bool CoreState::InitMicrophone() {
    retro_microphone_params_t params { 44100 };
    _microphone = _microphoneInterface.open_mic(&params);
    if (!_microphone) {
        _log(RETRO_LOG_ERROR, "Failed to open microphone\n");
        return false;
    }
    _log(RETRO_LOG_INFO, "Microphone initialized\n");

    if (!_microphoneInterface.set_mic_state(_microphone, true)) {
        _log(RETRO_LOG_ERROR, "Failed to enable microphone\n");
        return false;
    }
    _log(RETRO_LOG_INFO, "Microphone enabled\n");

    if (!_microphoneInterface.get_params(_microphone, &_actualMicParams)) {
        _log(RETRO_LOG_ERROR, "Failed to get microphone parameters\n");
        return false;
    }
    _log(RETRO_LOG_INFO, "Microphone parameters: rate = %u\n", _actualMicParams.rate);

    return true;
}

void CoreState::Run()
{
    if (!_micInitialized) {
        _micInitialized = InitMicrophone();
    }

    _input_poll();

    std::array<int16_t, SAMPLES_PER_FRAME> samples;
    int samplesRead = _microphoneInterface.read_mic(_microphone, samples.data(), samples.size());

    if (samplesRead > 0) {
        bool isBlowing = IsBlowing(std::span(samples.data(), samplesRead));
        retro_message_ext message {
            .msg = isBlowing ? "Blowing detected" : "No blowing",
            .duration = 20,
            .level = RETRO_LOG_INFO,
            .target = RETRO_MESSAGE_TARGET_OSD,
            .type = RETRO_MESSAGE_TYPE_STATUS,
            .progress = 75,
        };
        _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &message);
    }

    // TODO: Fill the background
    _video_refresh(_framebuffer->data, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(pntr_color));
    //_audio_sample_batch(outbuffer.data(), outbuffer.size() / 2);
}