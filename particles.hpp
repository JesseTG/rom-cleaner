#pragma once

#include <vector>
#include <span>

#include <pntr.h>
#include <random>

#include "battery/embed.hpp"

struct Particle {
    pntr_vector position {0, 0};
    pntr_vector velocity {0, 0};
    double timeToLive = 0.0;
    bool alive = false;
};

struct ParticleSystemArgs {
    size_t maxParticles;
    double spawnRate;
    double baseTimeToLive;
    pntr_vector baseVelocity;
    pntr_rectangle spawnArea;
};

class ParticleSystem {
public:
    ParticleSystem(const b::EmbedInternal::EmbeddedFile& file, const ParticleSystemArgs& args) noexcept;
    ParticleSystem(std::span<const uint8_t> image, const ParticleSystemArgs& args) noexcept;
    ~ParticleSystem() noexcept;
    ParticleSystem(ParticleSystem&) = delete;
    ParticleSystem(ParticleSystem&&) noexcept;
    ParticleSystem& operator=(ParticleSystem&) = delete;
    ParticleSystem& operator=(ParticleSystem&& other) noexcept;

    void Update(double dt);
    void Draw(pntr_image& framebuffer);
    void SetSpawnArea(pntr_rectangle area) noexcept;

    [[nodiscard]] pntr_rectangle GetSpawnArea() const noexcept { return _args.spawnArea; }
    void SetSpawning(bool spawning) noexcept { _spawning = spawning; }
    [[nodiscard]] bool IsSpawning() const noexcept { return _spawning; }

private:
    pntr_image* _image = nullptr;
    std::vector<Particle> _particles {};
    ParticleSystemArgs _args;
    std::default_random_engine _rng {std::random_device{}()};
    std::uniform_int_distribution<> _randomX;
    std::uniform_int_distribution<> _randomY;
    bool _spawning = false;

    void EmitParticle(double max);
    void UpdateSpawnArea();
};