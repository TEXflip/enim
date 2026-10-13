#pragma once

template<typename T>
struct Vec2 {
    T x = static_cast<T>(0);
    T y = static_cast<T>(0);

    Vec2() = default;
    Vec2(T x, T y) : x(x), y(y) {}
    Vec2(const Vec2&) = default;

    template<typename U>
    Vec2(U x, U y) : x(static_cast<T>(x)), y(static_cast<T>(y)) {}

    template<typename U>
    Vec2& operator=(const Vec2<U>& other) {
        x = static_cast<T>(other.x);
        y = static_cast<T>(other.y);
        return *this;
    }

    bool operator==(const Vec2& other) const;
    bool operator!=(const Vec2& other) const;

    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(const Vec2& other) const;
    Vec2 operator/(const Vec2& other) const;

    Vec2 operator+(T scalar) const;
    Vec2 operator-(T scalar) const;
    Vec2 operator*(T scalar) const;
    Vec2 operator/(T scalar) const;

    Vec2& operator+=(const Vec2& other);
    Vec2& operator-=(const Vec2& other);
    Vec2& operator*=(T scalar);
    Vec2& operator/=(T scalar);

    Vec2 operator-() const;
};

typedef Vec2<float> Vec2f;
typedef Vec2<int> Vec2i;

template<typename T>
inline Vec2<T> operator*(T scalar, const Vec2<T>& vec) {
    return vec * scalar;
}

template<typename T, typename U>
inline Vec2<T> operator+(Vec2<T> lhs, const Vec2<U>& rhs) {
    return Vec2<T>{lhs.x + static_cast<T>(rhs.x), lhs.y + static_cast<T>(rhs.y)};
}

template<typename T, typename U>
inline Vec2<T> operator-(Vec2<T> lhs, const Vec2<U>& rhs) {
    return Vec2<T>{lhs.x - static_cast<T>(rhs.x), lhs.y - static_cast<T>(rhs.y)};
}

