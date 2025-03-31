//
// Created by Jesse on 3/19/2025.
//

#include "blow.hpp"

#include "constants.hpp"

BlowDetector::BlowDetector() :
    _fftConfig(kiss_fft_alloc(SAMPLES_PER_FRAME, 0, nullptr, nullptr))
{
}

BlowDetector::BlowDetector(BlowDetector&& other) noexcept :
    _fftConfig(other._fftConfig),
    _adaptiveThreshold(other._adaptiveThreshold),
    _historyIndex(other._historyIndex),
    _detectionHistory(other._detectionHistory),
    _backgroundLevels(other._backgroundLevels),
    _bgIndex(other._bgIndex)
{
    other._fftConfig = nullptr;
}

BlowDetector& BlowDetector::operator=(BlowDetector&& other) noexcept {
    if (this != &other) {
        if (_fftConfig) {
            kiss_fft_free(_fftConfig);
        }
        _fftConfig = other._fftConfig;
        _adaptiveThreshold = other._adaptiveThreshold;
        _historyIndex = other._historyIndex;
        _detectionHistory = other._detectionHistory;
        _backgroundLevels = other._backgroundLevels;
        _bgIndex = other._bgIndex;

        other._fftConfig = nullptr;
    }
    return *this;
}

BlowDetector::~BlowDetector() {
    if (_fftConfig) {
        kiss_fft_free(_fftConfig);
        _fftConfig = nullptr;
    }
}

bool BlowDetector::IsBlowing(nonstd::span<const int16_t> samples) {
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
    _adaptiveThreshold = std::max<double>(RMS_THRESHOLD, avgBgNoise * 2.0); // Reduced multiplier

    // Early exit if too quiet
    if (rms < _adaptiveThreshold) {
        _detectionHistory[_historyIndex] = false;
        _historyIndex = (_historyIndex + 1) % SMOOTHING_FRAMES;
        return false;
    }

    // Convert samples for FFT with window function
    std::array<kiss_fft_cpx, SAMPLES_PER_FRAME> in, out;
    for (size_t i = 0; i < samples.size(); i++) {
        float window = 0.5f * (1.0f - cos(2.0f * M_PI * i / (samples.size() - 1)));
        in[i].r = static_cast<float>(samples[i]) / 32768.0f * window;
        in[i].i = 0;
    }

    // Execute FFT
    kiss_fft(_fftConfig, in.data(), out.data());

    // Analyze frequency content
    double bin_size = SAMPLE_RATE / static_cast<double>(samples.size());
    double low_freq_energy = 0.0, total_energy = 0.0;
    double blow_signature_energy = 0.0;
    double signature_peak = 0.0;

    // Skip DC component (i=0)
    for (int i = 1; i < samples.size() / 2; i++) {
        double freq = i * bin_size;
        double magnitude = sqrt(out[i].r * out[i].r + out[i].i * out[i].i);

        // Update background spectrum during quiet periods
        if (rms < _adaptiveThreshold * 0.8 && _spectrumUpdateCounter++ % 10 == 0) {
            _bgSpectrum[i] = _bgSpectrum[i] * 0.95 + magnitude * 0.05; // Slow update
        }

        // Subtract background noise profile (with floor)
        magnitude = std::max(0.0, magnitude - _bgSpectrum[i] * 1.2); // Reduced multiplier

        total_energy += magnitude;

        if (freq < LOW_FREQ_LIMIT) {
            low_freq_energy += magnitude;

            // Look for blow signature (focused energy in 150-500Hz range)
            if (freq > 150 && freq < 500) {
                blow_signature_energy += magnitude;
                signature_peak = std::max(signature_peak, magnitude);
            }
        }
    }

    // Skip detection if total energy is too low after noise reduction
    if (total_energy < 0.005) { // Lower energy threshold
        _detectionHistory[_historyIndex] = false;
        _historyIndex = (_historyIndex + 1) % SMOOTHING_FRAMES;
        return false;
    }

    // More balanced criteria for blow detection
    bool frequencyRatio = (total_energy > 0) && ((low_freq_energy / total_energy) > BLOW_RATIO);
    bool signatureStrength = (total_energy > 0) && ((blow_signature_energy / total_energy) > 0.3); // Lower threshold
    bool signaturePeak = signature_peak > (total_energy / samples.size() * 3.0); // Lower ratio

    // Use OR instead of AND to catch more potential blow patterns
    bool currentDetection = frequencyRatio || (signatureStrength && signaturePeak);

    // Update history
    _detectionHistory[_historyIndex] = currentDetection;
    _historyIndex = (_historyIndex + 1) % SMOOTHING_FRAMES;

    // Count positive detections in history
    int positiveCount = 0;
    for (bool detection : _detectionHistory) {
        if (detection) positiveCount++;
    }

    // Use a more balanced threshold that falls between 1/3 and 1/2
    // With SMOOTHING_FRAMES=6, this requires at least 2 positive frames
    return positiveCount >= 2;
}