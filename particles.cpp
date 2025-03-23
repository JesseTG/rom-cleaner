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
    _args(args),
    _randomX(args.spawnArea.x, args.spawnArea.x + args.spawnArea.width),
    _randomY(args.spawnArea.y, args.spawnArea.y + args.spawnArea.height),
    _randomImage(0, 0) // Initialize with single image range
{
    std::vector<std::span<const uint8_t>> images = {image};
    LoadImages(images);
    _particles.resize(_args.maxParticles);
}

ParticleSystem::ParticleSystem(std::span<const b::EmbedInternal::EmbeddedFile> files, const ParticleSystemArgs& args) noexcept :
    _args(args),
    _randomX(args.spawnArea.x, args.spawnArea.x + args.spawnArea.width),
    _randomY(args.spawnArea.y, args.spawnArea.y + args.spawnArea.height)
{
    std::vector<std::span<const uint8_t>> images;
    images.reserve(files.size());
    
    for (const auto& file : files) {
        images.push_back(std::span((const uint8_t*)file.data(), file.size()));
    }
    
    LoadImages(images);
    _particles.resize(_args.maxParticles);
}

ParticleSystem::ParticleSystem(std::span<std::span<const uint8_t>> images, const ParticleSystemArgs& args) noexcept :
    _args(args),
    _randomX(args.spawnArea.x, args.spawnArea.x + args.spawnArea.width),
    _randomY(args.spawnArea.y, args.spawnArea.y + args.spawnArea.height)
{
    LoadImages(images);
    _particles.resize(_args.maxParticles);
}

void ParticleSystem::LoadImages(std::span<std::span<const uint8_t>> images) {
    retro_assert(!images.empty());
    
    for (const auto& image : images) {
        pntr_image* img = pntr_load_image_from_memory(PNTR_IMAGE_TYPE_PNG, image.data(), image.size());
        retro_assert(img != nullptr);
        _images.push_back(img);
    }
    
    // Initialize the random distribution for selecting images
    _randomImage = std::uniform_int_distribution<size_t>(0, _images.size() - 1);
}

ParticleSystem::ParticleSystem(ParticleSystem&& other) noexcept :
    _images(std::move(other._images)),
    _particles(std::move(other._particles)),
    _args(other._args),
    _rng(other._rng),
    _randomX(other._randomX),
    _randomY(other._randomY),
    _randomImage(other._randomImage)
{
    // Clear the source images vector without deleting the images
    other._images.clear();
}

ParticleSystem& ParticleSystem::operator=(ParticleSystem&& other) noexcept {
    if (this != &other) {
        // Unload current images
        for (auto* img : _images) {
            if (img) {
                pntr_unload_image(img);
            }
        }
        
        _images = std::move(other._images);
        _particles = std::move(other._particles);
        _args = other._args;
        _rng = other._rng;
        _randomX = other._randomX;
        _randomY = other._randomY;
        _randomImage = other._randomImage;

        other._images.clear();
    }
    return *this;
}

ParticleSystem::~ParticleSystem() noexcept {
    for (auto* img : _images) {
        if (img) {
            pntr_unload_image(img);
        }
    }
    _images.clear();
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
            
            // Assign a random image to this particle
            p.imageIndex = _randomImage(_rng);

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
        if (p.alive && p.imageIndex < _images.size()) {
            pntr_draw_image(&framebuffer, _images[p.imageIndex], p.position.x, p.position.y);
        }
    }
}
