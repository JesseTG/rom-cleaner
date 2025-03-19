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

bool BlowDetector::IsBlowing(std::span<const int16_t> samples) {

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