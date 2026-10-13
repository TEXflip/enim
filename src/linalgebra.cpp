#include "linalgebra.hpp"

template<typename T>
bool Vec2<T>::operator==(const Vec2& other) const {
    return x == other.x && y == other.y;
}

template<typename T>
bool Vec2<T>::operator!=(const Vec2& other) const {
    return !(*this == other);
}

template<typename T>
Vec2<T> Vec2<T>::operator+(const Vec2& other) const {
    return Vec2{x + other.x, y + other.y};
}

template<typename T>
Vec2<T> Vec2<T>::operator-(const Vec2& other) const {
    return Vec2{x - other.x, y - other.y};
}

template<typename T>
Vec2<T> Vec2<T>::operator*(const Vec2& other) const {
    return Vec2{x * other.x, y * other.y};
}

template<typename T>
Vec2<T> Vec2<T>::operator/(const Vec2& other) const {
    return Vec2{x / other.x, y / other.y};
}

template<typename T>
Vec2<T> Vec2<T>::operator+(T scalar) const {
    return Vec2{x + scalar, y + scalar};
}

template<typename T>
Vec2<T> Vec2<T>::operator-(T scalar) const {
    return Vec2{x - scalar, y - scalar};
}

template<typename T>
Vec2<T> Vec2<T>::operator*(T scalar) const {
    return Vec2{x * scalar, y * scalar};
}

template<typename T>
Vec2<T> Vec2<T>::operator/(T scalar) const {
    return Vec2{x / scalar, y / scalar};
}

template<typename T>
Vec2<T>& Vec2<T>::operator+=(const Vec2& other) {
    x += other.x;
    y += other.y;
    return *this;
}

template<typename T>
Vec2<T>& Vec2<T>::operator-=(const Vec2& other) {
    x -= other.x;
    y -= other.y;
    return *this;
}

template<typename T>
Vec2<T>& Vec2<T>::operator*=(T scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
}

template<typename T>
Vec2<T>& Vec2<T>::operator/=(T scalar) {
    x /= scalar;
    y /= scalar;
    return *this;
}

template<typename T>
Vec2<T> Vec2<T>::operator-() const {
    return Vec2{-x, -y};
}

// Explicit template instantiations
template struct Vec2<float>;