#pragma once

#include <yasl/sys/types.h>

// Type traits and concepts built on compiler intrinsics instead of <type_traits>/<concepts>.

namespace yasl {
    namespace detail {
        template <class T>
        struct RemoveRefT {
            using Type = T;
        };
        template <class T>
        struct RemoveRefT<T&> {
            using Type = T;
        };
        template <class T>
        struct RemoveRefT<T&&> {
            using Type = T;
        };

        template <class T>
        struct RemoveCVT {
            using Type = T;
        };
        template <class T>
        struct RemoveCVT<const T> {
            using Type = T;
        };
        template <class T>
        struct RemoveCVT<volatile T> {
            using Type = T;
        };
        template <class T>
        struct RemoveCVT<const volatile T> {
            using Type = T;
        };

        template <bool B, class A, class C>
        struct CondT {
            using Type = A;
        };
        template <class A, class C>
        struct CondT<false, A, C> {
            using Type = C;
        };

        template <class A, class B>
        struct IsSameT {
            static constexpr bool value = false;
        };
        template <class A>
        struct IsSameT<A, A> {
            static constexpr bool value = true;
        };

        template <class T>
        struct IsIntegralT {
            static constexpr bool value = false;
        };
#define YASL_INTEGRAL(T)                         \
    template <>                                  \
    struct IsIntegralT<T> {                      \
        static constexpr bool value = true;      \
    };
        YASL_INTEGRAL(bool)
        YASL_INTEGRAL(char)
        YASL_INTEGRAL(signed char)
        YASL_INTEGRAL(unsigned char)
        YASL_INTEGRAL(wchar_t)
        YASL_INTEGRAL(char8_t)
        YASL_INTEGRAL(char16_t)
        YASL_INTEGRAL(char32_t)
        YASL_INTEGRAL(short)
        YASL_INTEGRAL(unsigned short)
        YASL_INTEGRAL(int)
        YASL_INTEGRAL(unsigned int)
        YASL_INTEGRAL(long)
        YASL_INTEGRAL(unsigned long)
        YASL_INTEGRAL(long long)
        YASL_INTEGRAL(unsigned long long)
#undef YASL_INTEGRAL

        template <class T>
        struct IsFloatT {
            static constexpr bool value = false;
        };
        template <>
        struct IsFloatT<float> {
            static constexpr bool value = true;
        };
        template <>
        struct IsFloatT<double> {
            static constexpr bool value = true;
        };
        template <>
        struct IsFloatT<long double> {
            static constexpr bool value = true;
        };

        template <class T>
        struct IsPointerT {
            static constexpr bool value = false;
        };
        template <class T>
        struct IsPointerT<T*> {
            static constexpr bool value = true;
        };

        template <class T>
        struct MakeUnsignedT;
#define YASL_UNSIGNED(S, U)      \
    template <>                  \
    struct MakeUnsignedT<S> {    \
        using Type = U;          \
    };
        YASL_UNSIGNED(char, unsigned char)
        YASL_UNSIGNED(signed char, unsigned char)
        YASL_UNSIGNED(unsigned char, unsigned char)
        YASL_UNSIGNED(short, unsigned short)
        YASL_UNSIGNED(unsigned short, unsigned short)
        YASL_UNSIGNED(int, unsigned int)
        YASL_UNSIGNED(unsigned int, unsigned int)
        YASL_UNSIGNED(long, unsigned long)
        YASL_UNSIGNED(unsigned long, unsigned long)
        YASL_UNSIGNED(long long, unsigned long long)
        YASL_UNSIGNED(unsigned long long, unsigned long long)
#undef YASL_UNSIGNED

        template <class T>
        void implicit_convert(T) noexcept;
    }

    template <class T>
    using RemoveReference = typename detail::RemoveRefT<T>::Type;

    template <class T>
    using RemoveCV = typename detail::RemoveCVT<T>::Type;

    template <class T>
    using RemoveCVRef = RemoveCV<RemoveReference<T>>;

    template <bool B, class A, class C>
    using Conditional = typename detail::CondT<B, A, C>::Type;

    template <class T>
    using MakeUnsigned = typename detail::MakeUnsignedT<RemoveCV<T>>::Type;

    template <class T>
    T&& declval() noexcept;

    template <class A, class B>
    concept SameAs = detail::IsSameT<A, B>::value && detail::IsSameT<B, A>::value;

    template <class T>
    concept Integral = detail::IsIntegralT<RemoveCV<T>>::value;

    template <class T>
    concept SignedIntegral = Integral<T> && (T(-1) < T(0));

    template <class T>
    concept UnsignedIntegral = Integral<T> && !SignedIntegral<T>;

    template <class T>
    concept FloatingPoint = detail::IsFloatT<RemoveCV<T>>::value;

    template <class T>
    concept Arithmetic = Integral<T> || FloatingPoint<T>;

    template <class T>
    concept Pointer = detail::IsPointerT<RemoveCV<T>>::value;

    template <class From, class To>
    concept ConvertibleTo = requires { detail::implicit_convert<To>(declval<From>()); };

    template <class T, class... Args>
    concept ConstructibleFrom = __is_constructible(T, Args...);

    template <class T>
    concept MoveConstructible = ConstructibleFrom<T, T&&>;

    template <class T>
    concept CopyConstructible = ConstructibleFrom<T, const T&>;

    template <class T>
    concept DefaultConstructible = ConstructibleFrom<T>;

    template <class T>
    concept NothrowMoveConstructible = __is_nothrow_constructible(T, T&&);

    template <class T>
    concept HasVirtualDestructor = __has_virtual_destructor(T);

    template <class Derived, class Base>
    concept DerivedFrom = __is_base_of(Base, Derived) && __is_convertible_to(const volatile Derived*, const volatile Base*);

    template <class T>
    concept TriviallyCopyable = __is_trivially_copyable(T);

#if defined(__has_builtin) && __has_builtin(__is_trivially_destructible)
    template <class T>
    concept TriviallyDestructible = __is_trivially_destructible(T);
#else
    template <class T>
    concept TriviallyDestructible = __has_trivial_destructor(T);
#endif

    template <class A, class B>
    concept EqualityComparableWith = requires(const A& a, const B& b) {
        { a == b } -> ConvertibleTo<bool>;
    };

    template <class T>
    concept EqualityComparable = EqualityComparableWith<T, T>;

    template <class T>
    concept ThreeWayComparable = requires(const T& a, const T& b) { a <=> b; };

    template <class F, class... Args>
    concept Invocable = requires(F&& f, Args&&... args) { static_cast<F&&>(f)(static_cast<Args&&>(args)...); };
}
