//
// Created by Jesse on 3/18/2025.
//

#include "cart.hpp"

#include <retro_assert.h>

Cart::Cart(nonstd::span<const uint8_t> image) noexcept :
    _image(pntr_load_image_from_memory(PNTR_IMAGE_TYPE_PNG, image.data(), image.size()))
{
    retro_assert(_image != nullptr);
}

Cart::Cart(Cart&& other) noexcept :
    _image(other._image),
    _position(other._position)
{
    other._image = nullptr;
}

Cart& Cart::operator=(Cart&& other) noexcept {
    if (this != &other) {
        if (_image) {
            pntr_unload_image(_image);
        }
        _image = other._image;
        _position = other._position;
        other._image = nullptr;
    }
    return *this;
}

Cart::~Cart() {
    pntr_unload_image(_image);
}

void Cart::Update() {

}

void Cart::Draw(pntr_image& framebuffer) {

    pntr_draw_image(&framebuffer, _image, _position.x, _position.y);

}