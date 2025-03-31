#include <array>
#include <cstddef>
#include <kiss_fft.h>
#include <memory>

#include <libretro.h>
#include <pntr.h>
#include <retro_assert.h>
#include <audio/audio_mixer.h>
#include <audio/conversion/float_to_s16.h>
#include <string/stdstring.h>

#include "blow.hpp"
#include "cart.hpp"
#include "constants.hpp"
#include "particles.hpp"

#include "embedded/romcleaner_cart_png.h"
#include "embedded/romcleaner_dust00_png.h"
#include "embedded/romcleaner_dust01_png.h"
#include "embedded/romcleaner_dust02_png.h"
#include "embedded/romcleaner_dust03_png.h"
#include "embedded/romcleaner_dust04_png.h"
#include "embedded/romcleaner_dust05_png.h"
#include "embedded/romcleaner_fanfare_wav.h"
#include "embedded/romcleaner_sparkle00_png.h"
#include "embedded/romcleaner_sparkle01_png.h"
#include "embedded/romcleaner_sparkle02_png.h"

using std::array;

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

// Define game states
enum class GameState {
    CART_ENTERING,  // Cart is animating into position
    CART_READY      // Cart is in position, ready for cleaning
};

struct CoreState
{
    CoreState() noexcept
    {
        _framebuffer = pntr_new_image(SCREEN_WIDTH, SCREEN_HEIGHT);
        retro_assert(_framebuffer != nullptr);

        _gradientBg = pntr_new_image(SCREEN_WIDTH, SCREEN_HEIGHT);
        pntr_draw_rectangle_gradient(_gradientBg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, PNTR_BLUE, PNTR_BLUE, PNTR_SKYBLUE, PNTR_SKYBLUE);

        audio_mixer_init(SAMPLE_RATE);

        _fanfareSound = audio_mixer_load_wav(
            (void*)embedded_romcleaner_fanfare_wav,
            sizeof(embedded_romcleaner_fanfare_wav),
            "sinc",
            RESAMPLER_QUALITY_DONTCARE
        );
        retro_assert(_fanfareSound != nullptr);
    }

    ~CoreState() noexcept
    {
        pntr_unload_image(_framebuffer);
        _framebuffer = nullptr;

        pntr_unload_image(_gradientBg);
        _gradientBg = nullptr;

        audio_mixer_stop(_fanfareVoice);
        _fanfareVoice = nullptr;

        audio_mixer_destroy(_fanfareSound);
        _fanfareSound = nullptr;

        audio_mixer_done();

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
    retro_microphone_interface _microphoneInterface {};
    retro_microphone* _microphone = nullptr;
    retro_microphone_params_t _actualMicParams {};
    std::unique_ptr<ParticleSystem> _particles = nullptr;
    std::unique_ptr<ParticleSystem> _sparkles = nullptr;  // Sparkle effect particles
    std::unique_ptr<Cart> _cart;
    bool _micInitialized = false;
    BlowDetector _blowDetector {};
    pntr_image* _framebuffer = nullptr;
    pntr_image* _gradientBg = nullptr;
    float _dustLevel = 100.0f;  // Track dust level from 0-100
    float _blowStrength = 0.0f; // Track how strongly player is blowing
    
    // Animation and state management
    GameState _gameState = GameState::CART_ENTERING;
    float _cartAnimationTime = 0.0f;
    float _cartAnimationDuration = 1.5f; // Duration of entrance animation in seconds
    pntr_vector _cartTargetPosition {}; // Target position for cart (center of screen)
    pntr_vector _cartStartPosition {};  // Starting position for cart (above screen)

    bool InitMicrophone();
    void Update();
    void Render();
    void UpdateDustLevel(bool isBlowing);
    void DisplayDustStatus();
    void UpdateCartAnimation();
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
    info->library_version = "1.0.0";
    info->valid_extensions = "sfc|smc|st|swc|bs|cgb|dmg|gb|gbc|sgb|a52|nes|3ds|3dsx|cart|rom|sms|bms|int|col|cv|md|mdx|smd|gen|gg|sg|gba|nds|lnx|lyx|pce|sgx|ws|wsc|vb|vboy|n64|z64|v64|vec";

    // We don't actually use the ROM, so no need to load or patch anything
    info->need_fullpath = true;
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
    retro_message_ext error {
        .msg = "April Fools!",
        .duration = 3000,
        .priority = 1000,
        .level = RETRO_LOG_INFO,
        .target = RETRO_MESSAGE_TARGET_ALL,
        .type = RETRO_MESSAGE_TYPE_NOTIFICATION,
    };

    _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &error);
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
    retro_message_ext error {};

    error.msg = e.what();
    error.duration = 3000;
    error.level = RETRO_LOG_ERROR;
    error.target = RETRO_MESSAGE_TARGET_ALL;
    error.type = RETRO_MESSAGE_TYPE_NOTIFICATION;

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

bool CoreState::LoadGame(const retro_game_info& game) {
    if (string_is_empty(game.path)) {
        throw std::runtime_error("No game path provided");
    }

    _microphoneInterface.interface_version = RETRO_MICROPHONE_INTERFACE_VERSION;
    if (!_environment(RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE, &_microphoneInterface)) {
        throw std::runtime_error("Failed to get microphone interface");
    }

    _cart = std::make_unique<Cart>(nonstd::span {embedded_romcleaner_cart_png, sizeof(embedded_romcleaner_cart_png)});

    // Calculate cart dimensions and positions
    pntr_vector cartSize = _cart->GetSize();

    _cartTargetPosition = {
        SCREEN_WIDTH / 2 - cartSize.x / 2,
        SCREEN_HEIGHT / 4 - cartSize.y / 4
    };
    
    // Set start position (above screen)
    _cartStartPosition = {
        _cartTargetPosition.x,
        -cartSize.y  // Start completely above the screen
    };
    
    // Initialize cart position to starting position
    _cart->SetPosition(_cartStartPosition);
    
    // Reset animation timer
    _cartAnimationTime = 0.0f;
    _gameState = GameState::CART_ENTERING;

    // Initialize particles with multiple dust images
    pntr_vector cartPos = _cart->GetPosition();
    
    std::array<nonstd::span<const uint8_t>, 6> dustImages = {
        nonstd::span {embedded_romcleaner_dust00_png, sizeof(embedded_romcleaner_dust00_png)},
        {embedded_romcleaner_dust01_png, sizeof(embedded_romcleaner_dust01_png)},
        {embedded_romcleaner_dust02_png, sizeof(embedded_romcleaner_dust02_png)},
        {embedded_romcleaner_dust03_png, sizeof(embedded_romcleaner_dust03_png)},
        {embedded_romcleaner_dust04_png, sizeof(embedded_romcleaner_dust04_png)},
        {embedded_romcleaner_dust05_png, sizeof(embedded_romcleaner_dust05_png)},
    };
    
    _particles = std::make_unique<ParticleSystem>(
        dustImages,
        ParticleSystemArgs {
            .maxParticles = 400,
            .spawnRate = 300,
            .baseTimeToLive = .75,
            .baseVelocity = { 0, 300 },
            .spawnArea = { cartPos.x, cartPos.y + cartSize.y, _cart->GetSize().x, 4 },
            .deceleration = 300.0,  // Strong deceleration for dust (px/s²)
            .edgeAngleOffset = 30
        }
    );

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
    if (!_micInitialized && _gameState == GameState::CART_READY) {
        _micInitialized = InitMicrophone();
    }

    _input_poll();

    Update();
    Render();
}

void CoreState::Update() {
    // Handle cart entry animation
    if (_gameState == GameState::CART_ENTERING) {
        UpdateCartAnimation();
    }

    // Only process microphone input when cart is in position
    if (_gameState == GameState::CART_READY) {
        std::array<int16_t, SAMPLES_PER_FRAME> samples {};
        int samplesRead = _microphoneInterface.read_mic(_microphone, samples.data(), samples.size());

        bool isBlowing = false;
        if (samplesRead > 0) {
            isBlowing = _blowDetector.IsBlowing(nonstd::span(samples.data(), samplesRead));
            
            // Instead of showing debug message, update dust level based on blowing
            if (isBlowing) {
                // Optionally, get blow intensity from detector if implemented
                _blowStrength = 1.0f; // Default value if intensity not available
            } else {
                _blowStrength = 0.0f;
            }
            
            // Update dust level based on blowing
            UpdateDustLevel(isBlowing);
            
            // Display current dust status to player
        }

        DisplayDustStatus();

        if (_particles) {
            // Set particle emission based on blow strength and remaining dust
            _particles->SetSpawning(isBlowing && _dustLevel > 0);

            // Adjust particle emission rate based on dust level
            if (_particles && isBlowing && _dustLevel > 0) {
                // More dust = more particles when blowing
                float emissionRate = (_dustLevel / 100.0f) * 1.0f; // Scale between 0 and 1
                // Note: You might need to modify ParticleSystem to support dynamic emission rate
            }
        }

        // If dust level reaches zero and we haven't created sparkles yet, create them
        if (_dustLevel <= 0 && !_sparkles) {
            // Create sparkle particle system
            std::array<nonstd::span<const uint8_t>, 3> sparkleImages = {
                nonstd::span { embedded_romcleaner_sparkle00_png, sizeof(embedded_romcleaner_sparkle00_png) },
                { embedded_romcleaner_sparkle01_png, sizeof(embedded_romcleaner_sparkle01_png) },
                { embedded_romcleaner_sparkle02_png, sizeof(embedded_romcleaner_sparkle02_png) },
            };

            pntr_vector cartPos = _cart->GetPosition();
            pntr_vector cartSize = _cart->GetSize();

            _sparkles = std::make_unique<ParticleSystem>(
                sparkleImages,
                ParticleSystemArgs {
                    .maxParticles = 40,
                    .spawnRate = 5,           // Spawn 5 sparkles per second
                    .baseTimeToLive = 0.5f,   // Short-lived sparkles
                    .baseVelocity = { 0, 0 }, // Sparkles don't move
                    .spawnArea = { cartPos.x, cartPos.y, cartSize.x, cartSize.y },
                }
            );

            _sparkles->SetSpawning(true);

            _fanfareVoice = audio_mixer_play(_fanfareSound, false, 1.0f, "sinc", RESAMPLER_QUALITY_DONTCARE, nullptr);
            retro_assert(_fanfareVoice != nullptr);
        }
    }

    if (_cart) {
        _cart->Update();
    }
    
    // Always update particles for continuous animation
    if (_particles) {
        _particles->Update(TIME_STEP);
    }
    
    // Update sparkles if they exist
    if (_sparkles) {
        _sparkles->Update(TIME_STEP);
    }
}

// New method to handle cart animation
void CoreState::UpdateCartAnimation() {
    _cartAnimationTime += TIME_STEP;
    
    if (_cartAnimationTime >= _cartAnimationDuration) {
        // Animation complete, set final position
        _cart->SetPosition(_cartTargetPosition);
        _gameState = GameState::CART_READY;
    } else {
        // Calculate eased position
        float progress = _cartAnimationTime / _cartAnimationDuration;
        
        // Apply easing function (ease-out cubic)
        float easedProgress = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
        
        // Interpolate position
        int x = _cartStartPosition.x + (int)(easedProgress * (_cartTargetPosition.x - _cartStartPosition.x));
        int y = _cartStartPosition.y + (int)(easedProgress * (_cartTargetPosition.y - _cartStartPosition.y));
        
        _cart->SetPosition(x, y);
        
        // Update particle spawn area to follow cart
        if (_particles) {
            pntr_vector cartPos = _cart->GetPosition();
            pntr_vector cartSize = _cart->GetSize();
            _particles->SetSpawnArea({
                cartPos.x, cartPos.y + cartSize.y, 
                cartSize.x, 4
            });
        }
    }
}

// New method to update dust level
void CoreState::UpdateDustLevel(bool isBlowing) {
    if (isBlowing && _dustLevel > 0) {
        // Decrease dust level when blowing, with a minimum of 0
        constexpr float decreaseRate = 85.0f; // Dust decrease per second when blowing
        _dustLevel -= decreaseRate * TIME_STEP;

        // TODO: Increase particle emission when dust is higher
        if (_particles) {
            // Implementation depends on your ParticleSystem class capabilities
        }
    }
}

// New method to display dust status
void CoreState::DisplayDustStatus() {
    retro_message_ext message = {};

    message.duration = 33; // Show continuously with short duration
    message.level = RETRO_LOG_INFO;
    message.target = RETRO_MESSAGE_TARGET_OSD;
    message.type = RETRO_MESSAGE_TYPE_PROGRESS;
    message.progress = static_cast<int8_t>(_dustLevel); // Use dust level for progress bar

    if (_dustLevel > 0) {
        message.msg = "Blow into the microphone to clean your ROM!";
    }
    else {
        // Show special message when cartridge is clean
        message.msg = "Your ROM is clean!";
    }

    _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &message);
}

void CoreState::Render() {
    pntr_draw_image(_framebuffer, _gradientBg, 0, 0);

    if (_cart) {
        _cart->Draw(*_framebuffer);
        // TODO: Shake the cart as the player blows into it
    }

    if (_particles) {
        _particles->Draw(*_framebuffer);
    }
    
    // Draw sparkles on top of everything if they exist
    if (_sparkles) {
        _sparkles->Draw(*_framebuffer);
    }

    array<float, SAMPLE_RATE * 2 / 60> buffer {};
    array<int16_t, SAMPLE_RATE * 2 / 60> outbuffer {};

    audio_mixer_mix(buffer.data(), buffer.size() / 2, 1.0f, false);
    convert_float_to_s16(outbuffer.data(), buffer.data(), buffer.size());

    _video_refresh(_framebuffer->data, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(pntr_color));
    _audio_sample_batch(outbuffer.data(), outbuffer.size() / 2);
}

