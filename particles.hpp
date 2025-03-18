#pragma once

#include <functional>
#include <vector>
#include <span>

#include <pntr.h>

struct Particle {
    pntr_vector position {0, 0};
    pntr_vector velocity {0, 0};
    double timeToLive = 0.0;
    bool alive = false;
};

using Emitter = std::function<Particle()>;

class ParticleSystem {
public:
    ParticleSystem(std::span<const uint8_t> image, Emitter emitter, size_t maxParticles) noexcept;
    ~ParticleSystem() noexcept;
    ParticleSystem(ParticleSystem&) = delete;
    ParticleSystem(ParticleSystem&&) noexcept;
    ParticleSystem& operator=(ParticleSystem&) = delete;
    ParticleSystem& operator=(ParticleSystem&& other) noexcept;

    void Update(double dt);
    void Draw(pntr_image& framebuffer);
    void SetSpawnArea(pntr_rectangle area) noexcept {
        _spawnArea = area;
    }

    [[nodiscard]] pntr_rectangle GetSpawnArea() const noexcept { return _spawnArea; }

private:
    pntr_image* _image = nullptr;
    pntr_rectangle _spawnArea {};
    std::vector<Particle> _particles {};
    Emitter _emitter;
    size_t _maxParticles;
};