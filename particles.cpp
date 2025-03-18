//
// Created by Jesse on 3/18/2025.
//

#include "particles.hpp"
#include "constants.hpp"

#include <cmath>
#include <retro_assert.h>

#include <utility>

ParticleSystem::ParticleSystem(std::span<const uint8_t> image, Emitter emitter, size_t maxParticles) noexcept :
    _image(pntr_load_image_from_memory(PNTR_IMAGE_TYPE_PNG, image.data(), image.size())),
    _emitter(std::move(emitter)),
    _maxParticles(maxParticles)
{
    retro_assert(_image != nullptr);

    _particles.resize(_maxParticles);
}

void ParticleSystem::Update(double dt) {
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

