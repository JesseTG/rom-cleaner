#pragma once

constexpr int SAMPLE_RATE = 44100;
constexpr int SCREEN_WIDTH = 1366;
constexpr int SCREEN_HEIGHT = 768;
constexpr double FPS = 60.0;
constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / FPS;
constexpr double TIME_STEP = 1.0f / FPS;