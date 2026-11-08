#pragma once
#include <concepts>
#include <cstdint>
#include <type_traits>

#if __has_include(<stdfloat>)
#  include <stdfloat>
#else
namespace std {
    using float32_t = float;
    using float64_t = double;
}
#endif

#ifdef __STDCPP_FLOAT32_T__
using Scalar = std::float32_t;
#else
using Scalar = float;
#endif

using IScalar = std::int32_t;

template<typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

template<Arithmetic T>
struct Vec2 {
    T x{};
    T y{};

    constexpr Vec2() = default;
    constexpr Vec2(T x, T y) : x(x), y(y) {}

    template<Arithmetic A, Arithmetic B>
        requires (!std::same_as<A, T> || !std::same_as<B, T>)
    constexpr Vec2(A x, B y) : x(static_cast<T>(x)), y(static_cast<T>(y)) {}

    template<Arithmetic U>
        requires (!std::same_as<T, U>)
    constexpr explicit(std::floating_point<U> && std::integral<T>)
    Vec2(const Vec2<U>& o) : x(static_cast<T>(o.x)), y(static_cast<T>(o.y)) {}

    template<Arithmetic U>
        requires (!std::same_as<T, U>)
    constexpr Vec2& operator=(const Vec2<U>& o) {
        x = static_cast<T>(o.x);
        y = static_cast<T>(o.y);
        return *this;
    }

    // Defined in Vec2.cpp
    template<Arithmetic U> Vec2& operator+=(const Vec2<U>& o);
    template<Arithmetic U> Vec2& operator-=(const Vec2<U>& o);
    template<Arithmetic U> Vec2& operator*=(const Vec2<U>& o);
    template<Arithmetic U> Vec2& operator/=(const Vec2<U>& o);
    template<Arithmetic S> Vec2& operator*=(S s);
    template<Arithmetic S> Vec2& operator/=(S s);

    constexpr Vec2 operator-() const { return {static_cast<T>(-x), static_cast<T>(-y)}; }
    constexpr Vec2 operator+() const { return *this; }
};

template<Arithmetic T> Vec2(T, T) -> Vec2<T>;

using Vec2f = Vec2<Scalar>;
using Vec2i = Vec2<std::int32_t>;

namespace detail {
    template<Arithmetic T, Arithmetic U>
    using Common = Vec2<std::common_type_t<T, U>>;
}

// Defined in linalgebra.cpp
#define VEC2_DECL_OP(OP)                                                          \
template<Arithmetic T, Arithmetic U>                                              \
detail::Common<T, U> operator OP(const Vec2<T>& a, const Vec2<U>& b);             \
template<Arithmetic T, Arithmetic S>                                              \
detail::Common<T, S> operator OP(const Vec2<T>& v, S s);                          \
template<Arithmetic S, Arithmetic T>                                              \
detail::Common<T, S> operator OP(S s, const Vec2<T>& v);

VEC2_DECL_OP(+) VEC2_DECL_OP(-) VEC2_DECL_OP(*) VEC2_DECL_OP(/)
#undef VEC2_DECL_OP

template<Arithmetic T, Arithmetic U>
bool operator==(const Vec2<T>& a, const Vec2<U>& b);

// Extern template declarations to prevent implicit instantiation
// and force use of explicit instantiations in linalgebra.cpp
#define VEC2_EXTERN_VV(T, U) \
    extern template Vec2<T>& Vec2<T>::operator+=<U>(const Vec2<U>&); \
    extern template Vec2<T>& Vec2<T>::operator-=<U>(const Vec2<U>&); \
    extern template Vec2<T>& Vec2<T>::operator*=<U>(const Vec2<U>&); \
    extern template Vec2<T>& Vec2<T>::operator/=<U>(const Vec2<U>&); \
    extern template Vec2<std::common_type_t<T, U>> operator+<T, U>(const Vec2<T>&, const Vec2<U>&); \
    extern template Vec2<std::common_type_t<T, U>> operator-<T, U>(const Vec2<T>&, const Vec2<U>&); \
    extern template Vec2<std::common_type_t<T, U>> operator*<T, U>(const Vec2<T>&, const Vec2<U>&); \
    extern template Vec2<std::common_type_t<T, U>> operator/<T, U>(const Vec2<T>&, const Vec2<U>&); \
    extern template bool operator==<T, U>(const Vec2<T>&, const Vec2<U>&);

#define VEC2_EXTERN_VS(T, S) \
    extern template Vec2<T>& Vec2<T>::operator*=<S>(S); \
    extern template Vec2<T>& Vec2<T>::operator/=<S>(S); \
    extern template Vec2<std::common_type_t<T, S>> operator+<T, S>(const Vec2<T>&, S); \
    extern template Vec2<std::common_type_t<T, S>> operator-<T, S>(const Vec2<T>&, S); \
    extern template Vec2<std::common_type_t<T, S>> operator*<T, S>(const Vec2<T>&, S); \
    extern template Vec2<std::common_type_t<T, S>> operator/<T, S>(const Vec2<T>&, S); \
    extern template Vec2<std::common_type_t<T, S>> operator+<S, T>(S, const Vec2<T>&); \
    extern template Vec2<std::common_type_t<T, S>> operator-<S, T>(S, const Vec2<T>&); \
    extern template Vec2<std::common_type_t<T, S>> operator*<S, T>(S, const Vec2<T>&); \
    extern template Vec2<std::common_type_t<T, S>> operator/<S, T>(S, const Vec2<T>&);

VEC2_EXTERN_VV(Scalar, Scalar)
VEC2_EXTERN_VV(Scalar, IScalar)
VEC2_EXTERN_VV(IScalar, Scalar)
VEC2_EXTERN_VV(IScalar, IScalar)
VEC2_EXTERN_VS(Scalar, int)
VEC2_EXTERN_VS(Scalar, IScalar)
VEC2_EXTERN_VS(Scalar, float)
VEC2_EXTERN_VS(Scalar, double)
#ifdef __STDCPP_FLOAT32_T__
VEC2_EXTERN_VS(Scalar, Scalar)
#endif
VEC2_EXTERN_VS(IScalar, int)
VEC2_EXTERN_VS(IScalar, IScalar)
VEC2_EXTERN_VS(IScalar, float)
VEC2_EXTERN_VS(IScalar, double)
#ifdef __STDCPP_FLOAT32_T__
VEC2_EXTERN_VS(IScalar, Scalar)
#endif

#undef VEC2_EXTERN_VV
#undef VEC2_EXTERN_VS