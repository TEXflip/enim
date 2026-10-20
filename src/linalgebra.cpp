#include "linalgebra.hpp"

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

// ---- Compound assignment (result keeps lhs type) ----
#define VEC2_DEF_COMPOUND_VV(OP)                                                  \
template<Arithmetic T>                                                            \
template<Arithmetic U>                                                            \
Vec2<T>& Vec2<T>::operator OP##=(const Vec2<U>& o) {                              \
    using R = std::common_type_t<T, U>;                                           \
    x = static_cast<T>(static_cast<R>(x) OP static_cast<R>(o.x));                 \
    y = static_cast<T>(static_cast<R>(y) OP static_cast<R>(o.y));                 \
    return *this;                                                                 \
}

VEC2_DEF_COMPOUND_VV(+) VEC2_DEF_COMPOUND_VV(-)
VEC2_DEF_COMPOUND_VV(*) VEC2_DEF_COMPOUND_VV(/)
#undef VEC2_DEF_COMPOUND_VV

#define VEC2_DEF_COMPOUND_VS(OP)                                                  \
template<Arithmetic T>                                                            \
template<Arithmetic S>                                                            \
Vec2<T>& Vec2<T>::operator OP##=(S s) {                                           \
    using R = std::common_type_t<T, S>;                                           \
    x = static_cast<T>(static_cast<R>(x) OP static_cast<R>(s));                   \
    y = static_cast<T>(static_cast<R>(y) OP static_cast<R>(s));                   \
    return *this;                                                                 \
}

VEC2_DEF_COMPOUND_VS(*) VEC2_DEF_COMPOUND_VS(/)
#undef VEC2_DEF_COMPOUND_VS

// ---- Binary operators ----
#define VEC2_DEF_BINOP(OP)                                                        \
template<Arithmetic T, Arithmetic U>                                              \
detail::Common<T, U> operator OP(const Vec2<T>& a, const Vec2<U>& b) {            \
    using R = std::common_type_t<T, U>;                                           \
    return {static_cast<R>(static_cast<R>(a.x) OP static_cast<R>(b.x)),           \
            static_cast<R>(static_cast<R>(a.y) OP static_cast<R>(b.y))};          \
}                                                                                 \
template<Arithmetic T, Arithmetic S>                                              \
detail::Common<T, S> operator OP(const Vec2<T>& v, S s) {                         \
    using R = std::common_type_t<T, S>;                                           \
    return {static_cast<R>(static_cast<R>(v.x) OP static_cast<R>(s)),             \
            static_cast<R>(static_cast<R>(v.y) OP static_cast<R>(s))};            \
}                                                                                 \
template<Arithmetic S, Arithmetic T>                                              \
detail::Common<T, S> operator OP(S s, const Vec2<T>& v) {                         \
    using R = std::common_type_t<T, S>;                                           \
    return {static_cast<R>(static_cast<R>(s) OP static_cast<R>(v.x)),             \
            static_cast<R>(static_cast<R>(s) OP static_cast<R>(v.y))};            \
}

VEC2_DEF_BINOP(+) VEC2_DEF_BINOP(-) VEC2_DEF_BINOP(*) VEC2_DEF_BINOP(/)
#undef VEC2_DEF_BINOP

template<Arithmetic T, Arithmetic U>
bool operator==(const Vec2<T>& a, const Vec2<U>& b) {
    using R = std::common_type_t<T, U>;
    return static_cast<R>(a.x) == static_cast<R>(b.x)
        && static_cast<R>(a.y) == static_cast<R>(b.y);
}

// ---------------------------------------------------------------------------
// Explicit instantiations
//
// Component types: the types Vec2<T> may be instantiated with.
// Scalar types:    the types allowed in `vec * s`, `s * vec`, `vec *= s`, ...
//                  (include int/double so plain literals like `2` and `0.5` link)
// ---------------------------------------------------------------------------

#ifdef __STDCPP_FLOAT32_T__
    #define VEC2_EXTRA_SCALAR(M, T) M(T, Scalar)   // distinct from float
#else
    #define VEC2_EXTRA_SCALAR(M, T)                // Scalar is float already
#endif

#define VEC2_SCALARS(M, T)                                                        \
    M(T, IScalar) M(T, float) M(T, double)                                   \
    VEC2_EXTRA_SCALAR(M, T)

#define VEC2_COMPONENT_PAIRS(M)                                                   \
    M(Scalar, Scalar)       M(Scalar, IScalar)                               \
    M(IScalar, Scalar) M(IScalar, IScalar)

// Vec (op) Vec
#define VEC2_INST_VV(T, U)                                                        \
    template Vec2<T>& Vec2<T>::operator+=<U>(const Vec2<U>&);                     \
    template Vec2<T>& Vec2<T>::operator-=<U>(const Vec2<U>&);                     \
    template Vec2<T>& Vec2<T>::operator*=<U>(const Vec2<U>&);                     \
    template Vec2<T>& Vec2<T>::operator/=<U>(const Vec2<U>&);                     \
    template Vec2<std::common_type_t<T, U>> operator+<T, U>(const Vec2<T>&, const Vec2<U>&); \
    template Vec2<std::common_type_t<T, U>> operator-<T, U>(const Vec2<T>&, const Vec2<U>&); \
    template Vec2<std::common_type_t<T, U>> operator*<T, U>(const Vec2<T>&, const Vec2<U>&); \
    template Vec2<std::common_type_t<T, U>> operator/<T, U>(const Vec2<T>&, const Vec2<U>&); \
    template bool operator==<T, U>(const Vec2<T>&, const Vec2<U>&);

// Vec (op) scalar, scalar (op) Vec
#define VEC2_INST_VS(T, S)                                                        \
    template Vec2<T>& Vec2<T>::operator*=<S>(S);                                  \
    template Vec2<T>& Vec2<T>::operator/=<S>(S);                                  \
    template Vec2<std::common_type_t<T, S>> operator+<T, S>(const Vec2<T>&, S);   \
    template Vec2<std::common_type_t<T, S>> operator-<T, S>(const Vec2<T>&, S);   \
    template Vec2<std::common_type_t<T, S>> operator*<T, S>(const Vec2<T>&, S);   \
    template Vec2<std::common_type_t<T, S>> operator/<T, S>(const Vec2<T>&, S);   \
    template Vec2<std::common_type_t<T, S>> operator+<S, T>(S, const Vec2<T>&);   \
    template Vec2<std::common_type_t<T, S>> operator-<S, T>(S, const Vec2<T>&);   \
    template Vec2<std::common_type_t<T, S>> operator*<S, T>(S, const Vec2<T>&);   \
    template Vec2<std::common_type_t<T, S>> operator/<S, T>(S, const Vec2<T>&);

VEC2_COMPONENT_PAIRS(VEC2_INST_VV)
VEC2_SCALARS(VEC2_INST_VS, Scalar)
VEC2_SCALARS(VEC2_INST_VS, IScalar)

// Explicit instantiation for mixed float types (std::float32_t and float)
#ifdef __STDCPP_FLOAT32_T__
template detail::Common<Scalar, float> operator-<Scalar, float>(const Vec2<Scalar>&, const Vec2<float>&);
template detail::Common<float, Scalar> operator-<float, Scalar>(const Vec2<float>&, const Vec2<Scalar>&);
#endif

#undef VEC2_INST_VV
#undef VEC2_INST_VS
#undef VEC2_COMPONENT_PAIRS
#undef VEC2_SCALARS
#undef VEC2_EXTRA_SCALAR