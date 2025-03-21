#pragma once

#include <array>
#include <cstdint>
#include <kiss_fft.h>
#include <span>

#include "constants.hpp"

static constexpr int RMS_THRESHOLD = 80;  // Further lowered threshold
static constexpr float BLOW_RATIO = 0.55f; // More lenient ratio
static constexpr int SMOOTHING_FRAMES = 6;
static constexpr int LOW_FREQ_LIMIT = 600;  // Expanded range
static constexpr int ADAPTIVE_WINDOW = 30;  // For background noise estimation

class BlowDetector {
public:
    BlowDetector();
    ~BlowDetector();
    BlowDetector(const BlowDetector&) = delete;
    BlowDetector(BlowDetector&& other) noexcept;
    BlowDetector& operator=(const BlowDetector&) = delete;
    BlowDetector& operator=(BlowDetector&& other) noexcept;
    bool IsBlowing(std::span<const int16_t> samples);

private:
    kiss_fft_cfg _fftConfig = nullptr;
    double _adaptiveThreshold = RMS_THRESHOLD;
    size_t _historyIndex = 0;
    std::array<bool, SMOOTHING_FRAMES> _detectionHistory = {};
    std::array<double, ADAPTIVE_WINDOW> _backgroundLevels = {};
    size_t _bgIndex = 0;
    std::array<double, SAMPLES_PER_FRAME/2> _bgSpectrum {};
    int _spectrumUpdateCounter = 0;
};