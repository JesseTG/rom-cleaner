//
// Created by Jesse on 3/18/2025.
//

#ifndef CART_HPP
#define CART_HPP

#include <cstdint>
#include <span>
#include <pntr.h>

#include <nonstd/span.hpp>

class Cart {
public:
    Cart(nonstd::span<const uint8_t> image) noexcept;
    ~Cart();
    Cart(const Cart&) = delete;
    Cart& operator=(const Cart&) = delete;
    Cart(Cart&&) noexcept;
    Cart& operator=(Cart&&) noexcept;

    void Update();
    void Draw(pntr_image& framebuffer);

    void SetPosition(int x, int y) {
        _position.x = x;
        _position.y = y;
    }

    void SetPosition(pntr_vector position) {
        _position = position;
    }

    [[nodiscard]] pntr_vector GetPosition() const {
        return _position;
    }

    [[nodiscard]] pntr_vector GetSize() const {
        return { _image->width, _image->height };
    }

private:
    pntr_image* _image = nullptr;
    pntr_vector _position {};
};

#endif //CART_HPP
