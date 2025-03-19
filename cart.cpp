//
// Created by Jesse on 3/18/2025.
//

#include "cart.hpp"

#include <retro_assert.h>

#include "constants.hpp"

Cart::Cart(std::span<const uint8_t> image) noexcept :
    _image(pntr_load_image_from_memory(PNTR_IMAGE_TYPE_PNG, image.data(), image.size()))
{
    retro_assert(_image != nullptr);
}

Cart::~Cart() {
    pntr_unload_image(_image);
}

void Cart::Update() {

}

void Cart::Draw(pntr_image& framebuffer) {

    pntr_draw_image(&framebuffer, _image, _position.x, _position.y);

}