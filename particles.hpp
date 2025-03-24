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
    size_t imageIndex = 0;  // Index of the image to use for this particle
    double deceleration = 0.0; // Deceleration factor for this particle
};

struct ParticleSystemArgs {
    size_t maxParticles;
    double spawnRate;
    double baseTimeToLive;
    pntr_vector baseVelocity;
    pntr_rectangle spawnArea;
    double deceleration = 0.0;      // Deceleration factor (velocity reduction per second)
    double edgeAngleOffset = 5.0;   // Maximum angle offset at edges (in degrees)
};

class ParticleSystem {
public:
    ParticleSystem(const b::EmbedInternal::EmbeddedFile& file, const ParticleSystemArgs& args) noexcept;
    ParticleSystem(std::span<const uint8_t> image, const ParticleSystemArgs& args) noexcept;

    ParticleSystem(std::span<const b::EmbedInternal::EmbeddedFile> files, const ParticleSystemArgs& args) noexcept;
    ParticleSystem(std::span<std::span<const uint8_t>> images, const ParticleSystemArgs& args) noexcept;
    
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
    std::vector<pntr_image*> _images;  // Vector of particle images
    std::vector<Particle> _particles {};
    ParticleSystemArgs _args;
    std::default_random_engine _rng {std::random_device{}()};
    std::uniform_int_distribution<> _randomX;
    std::uniform_int_distribution<> _randomY;
    std::uniform_int_distribution<size_t> _randomImage;  // For selecting a random image
    bool _spawning = false;

    void EmitParticle(double max);
    void UpdateSpawnArea();
    void LoadImages(std::span<std::span<const uint8_t>> images);
};
