// MIT-licensed, see LICENSE in the root directory.

#include <array>
#include <cstddef>
#include <kiss_fft.h>
#include <memory>

#include <battery/embed.hpp>
#include <libretro.h>
#include <pntr.h>
#include <retro_assert.h>
#include <span>
#include <audio/audio_mixer.h>
#include <file/file_path.h>
#include <string/stdstring.h>

#include "blow.hpp"
#include "cart.hpp"
#include "constants.hpp"
#include "particles.hpp"

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

        // Initialize dust level to maximum (100%)
        _dustLevel = 100.0f;
        
        // Set initial game state to cart entering
        _gameState = GameState::CART_ENTERING;
    }

    ~CoreState() noexcept
    {
        pntr_unload_image(_framebuffer);
        _framebuffer = nullptr;

        pntr_unload_image(_gradientBg);
        _gradientBg = nullptr;

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
        auto data = b::embed<"png/snes.png">();
        _cart = std::make_unique<Cart>(std::span{(const uint8_t*)data.data(), data.size()});
    }

    if (!_cart) {
        // Warn the player that this game isn't supported
        retro_message_ext message {
            .msg = "This type of ROM isn't supported. Try another platform.",
            .duration = 3000,
            .level = RETRO_LOG_ERROR,
            .target = RETRO_MESSAGE_TARGET_OSD,
            .type = RETRO_MESSAGE_TYPE_NOTIFICATION,
            .progress = 0,
        };
        _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &message);
        return false;
    }

    // Calculate cart dimensions and positions
    pntr_vector cartSize = _cart->GetSize();
    
    // Set target position (center of screen)
    _cartTargetPosition = {
        SCREEN_WIDTH / 2 - cartSize.x / 2,
        SCREEN_HEIGHT / 2 - cartSize.y / 2
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

    // Initialize particles but don't emit them yet
    pntr_vector cartPos = _cart->GetPosition();
    _particles = std::make_unique<ParticleSystem>(
        b::embed<"dust00.png">(),
        ParticleSystemArgs {
            .maxParticles = 100,
            .spawnRate = 1,
            .baseTimeToLive = 1.0,
            .baseVelocity = { 0, 200 },
            .spawnArea = { cartPos.x, cartPos.y + cartSize.y, _cart->GetSize().x, 4 },
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
            isBlowing = _blowDetector.IsBlowing(std::span(samples.data(), samplesRead));
            
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
            DisplayDustStatus();
        }

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
    }

    if (_cart) {
        _cart->Update();
    }
    
    // Always update particles for continuous animation
    if (_particles) {
        _particles->Update(TIME_STEP);
    }
}

// New method to handle cart animation
void CoreState::UpdateCartAnimation() {
    _cartAnimationTime += TIME_STEP;
    
    if (_cartAnimationTime >= _cartAnimationDuration) {
        // Animation complete, set final position
        _cart->SetPosition(_cartTargetPosition);
        _gameState = GameState::CART_READY;
        
        // Show a message when cart is ready
        retro_message_ext message {
            .msg = "Blow into the microphone to clean your ROM!",
            .duration = 3000,
            .level = RETRO_LOG_INFO,
            .target = RETRO_MESSAGE_TARGET_OSD,
            .type = RETRO_MESSAGE_TYPE_NOTIFICATION,
            .progress = 0,
        };
        _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &message);
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
        constexpr float decreaseRate = 25.0f; // Dust decrease per second when blowing
        _dustLevel -= decreaseRate * TIME_STEP;
        
        // Increase particle emission when dust is higher
        if (_particles) {
            // Implementation depends on your ParticleSystem class capabilities
        }
    }
}

// New method to display dust status
void CoreState::DisplayDustStatus() {
    // Format message to show dust level
    char message[64];
    snprintf(message, sizeof(message), "Dust: %d%%", static_cast<int>(_dustLevel));

    retro_message_ext dustMessage {
        .msg = message,
        .duration = 60, // Show continuously with short duration
        .level = RETRO_LOG_INFO,
        .target = RETRO_MESSAGE_TARGET_OSD,
        .type = RETRO_MESSAGE_TYPE_PROGRESS,
        .progress = static_cast<int8_t>(_dustLevel), // Use dust level for progress bar
    };
    _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &dustMessage);
    
    // Show special message when cartridge is clean
    if (_dustLevel <= 0) {
        retro_message_ext cleanMessage {
            .msg = "Your ROM is now clean!",
            .duration = 3000,
            .level = RETRO_LOG_INFO,
            .target = RETRO_MESSAGE_TARGET_OSD,
            .type = RETRO_MESSAGE_TYPE_PROGRESS,
            .progress = -1,
        };
        _environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &cleanMessage);
    }
}

void CoreState::Render() {
    pntr_draw_image(_framebuffer, _gradientBg, 0, 0);

    if (_cart) {
        _cart->Draw(*_framebuffer);
        // TODO: Shake the cart as the player blows into it
        // TODO: Sparkle once the cart is dust-free
    }

    if (_particles) {
        _particles->Draw(*_framebuffer);
    }

    _video_refresh(_framebuffer->data, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(pntr_color));
    //_audio_sample_batch(outbuffer.data(), outbuffer.size() / 2);
}

