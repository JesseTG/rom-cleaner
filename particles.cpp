//
// Created by Jesse on 3/18/2025.
//

#include "particles.hpp"
#include "constants.hpp"

#include <cmath>
#include <retro_assert.h>

#include <utility>


ParticleSystem::ParticleSystem(const b::EmbedInternal::EmbeddedFile& file, const ParticleSystemArgs& args) noexcept :
    ParticleSystem(std::span((const uint8_t*)file.data(), file.size()), args) {
}

ParticleSystem::ParticleSystem(std::span<const uint8_t> image, const ParticleSystemArgs& args) noexcept :
    _image(pntr_load_image_from_memory(PNTR_IMAGE_TYPE_PNG, image.data(), image.size())),
    _args(args),
    _randomX(args.spawnArea.x, args.spawnArea.x + args.spawnArea.width),
    _randomY(args.spawnArea.y, args.spawnArea.y + args.spawnArea.height)
{
    retro_assert(_image != nullptr);

    _particles.resize(_args.maxParticles);
}

ParticleSystem::ParticleSystem(ParticleSystem&& other) noexcept :
    _image(other._image),
    _particles(std::move(other._particles)),
    _args(other._args),
    _rng(other._rng),
    _randomX(other._randomX),
    _randomY(other._randomY)
{
    other._image = nullptr;
}

ParticleSystem& ParticleSystem::operator=(ParticleSystem&& other) noexcept {
    if (this != &other) {
        if (_image) {
            pntr_unload_image(_image);
        }
        _image = other._image;
        _particles = std::move(other._particles);
        _args = other._args;
        _rng = other._rng;
        _randomX = other._randomX;
        _randomY = other._randomY;

        other._image = nullptr;
    }
    return *this;
}

ParticleSystem::~ParticleSystem() noexcept {
    if (_image) {
        pntr_unload_image(_image);
        _image = nullptr;
    }
}

void ParticleSystem::SetSpawnArea(pntr_rectangle area) noexcept {
    _args.spawnArea = area;
    UpdateSpawnArea();
}

void ParticleSystem::UpdateSpawnArea() {
    _randomX = std::uniform_int_distribution(_args.spawnArea.x, _args.spawnArea.x + _args.spawnArea.width);
    _randomY = std::uniform_int_distribution(_args.spawnArea.y, _args.spawnArea.y + _args.spawnArea.height);
}

void ParticleSystem::EmitParticle(double max) {
    // Find an inactive particle
    size_t particlesSpawned = 0;
    for (Particle& p : _particles) {
        if (particlesSpawned >= max)
            break;

        if (!p.alive) {
            p.position.x = _randomX(_rng);
            p.position.y = _randomY(_rng);

            // Randomize velocity within emitter's range
            p.velocity.x = _args.baseVelocity.x;
            p.velocity.y = _args.baseVelocity.y;

            // Set lifetime
            p.timeToLive = _args.baseTimeToLive;

            p.alive = true;
            ++particlesSpawned;
        }
    }
}

void ParticleSystem::Update(double dt) {
    // Emit new particles based on emission rate
    if (_spawning) {
        EmitParticle(_args.spawnRate * dt);
    }

    for (Particle& p : _particles) {
        if (p.alive) {
            p.timeToLive -= dt;
            p.alive = p.timeToLive > 0;
        }

        if (p.alive) {
            p.position.x += std::round(p.velocity.x * dt);
            p.position.y += std::round(p.velocity.y * dt);
        }
    }
}

void ParticleSystem::Draw(pntr_image& framebuffer) {
    for (const Particle& p : _particles) {
        if (p.alive) {
            pntr_draw_image(&framebuffer, _image, p.position.x, p.position.y);
            // pntr_draw_image already clips to ensure it doesn't blit outside the target
        }
    }
}

