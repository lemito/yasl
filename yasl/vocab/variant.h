#pragma once

#include <yasl/core/check.h>
#include <yasl/core/exception.h>
#include <yasl/core/utility.h>

namespace yasl {
namespace detail {
template <class T, class... Ts>
struct IndexOf;

template <class T, class... Ts>
inline constexpr size_t kIndexOf = IndexOf<T, Ts...>::value;

template <class T, class H, class... Ts>
struct IndexOf<T, H, Ts...> {
  static constexpr size_t value =
      SameAs<T, H> ? 0 : 1 + IndexOf<T, Ts...>::value;
};

template <class T>
struct IndexOf<T> {
  static constexpr size_t value = 0;
};

template <class T, class... Ts>
inline constexpr size_t kCountOf = (size_t(0) + ... + (SameAs<T, Ts> ? 1 : 0));

template <size_t I, class... Ts>
struct TypeAt;

template <class H, class... Ts>
struct TypeAt<0, H, Ts...> {
  using Type = H;
};

template <size_t I, class H, class... Ts>
struct TypeAt<I, H, Ts...> {
  using Type = typename TypeAt<I - 1, Ts...>::Type;
};

template <size_t... Is>
constexpr size_t max_of() noexcept {
  size_t m = 0;
  ((m = Is > m ? Is : m), ...);
  return m;
}

// Which alternative a value of type U initializes: the exact type, else the
// only alternative U converts to.
template <class U, class... Ts>
constexpr size_t pick_alternative() noexcept {
  using D = RemoveCVRef<U>;
  if constexpr (kCountOf<D, Ts...> == 1) {
    return kIndexOf<D, Ts...>;
  } else {
    constexpr size_t convertible =
        (size_t(0) + ... + (ConstructibleFrom<Ts, U> ? 1 : 0));
    static_assert(convertible == 1,
                  "Variant: no unique alternative for this value");
    size_t i = 0;
    size_t found = 0;
    ((ConstructibleFrom<Ts, U> ? (found = i, 0) : 0, ++i), ...);
    return found;
  }
}
}  // namespace detail

// Combines lambdas into one callable for match():
//
//     match(v, overloaded{
//         [](int i) { ... },
//         [](const String& s) { ... },
//     });
template <class... Fs>
struct overloaded : Fs... {
  using Fs::operator()...;
};

template <class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

// A value of exactly one of the types Ts (Rust enum, tagged union). Holds its
// alternative inline. Assignment destroys the old alternative first; if
// constructing the new one throws the variant ends up valueless (valueless() ==
// true), so give alternatives noexcept moves.
//
//     Variant<int, String> v = String("x");
//     if (String* s = v.get<String>()) ...
//     v.match([](int) {...}, [](String&) {...});
template <class... Ts>
class Variant {
  static_assert(sizeof...(Ts) > 0 && sizeof...(Ts) < 255);
  static constexpr uint8 kValueless = 255;

 public:
  Variant()
    requires DefaultConstructible<typename detail::TypeAt<0, Ts...>::Type>
  {
    using First = typename detail::TypeAt<0, Ts...>::Type;
    yasl::construct_at(reinterpret_cast<First*>(storage_));
    index_ = 0;
  }

  template <class U>
    requires(!SameAs<RemoveCVRef<U>, Variant>)
  Variant(U&& value) {
    constexpr size_t I = detail::pick_alternative<U, Ts...>();
    using T = typename detail::TypeAt<I, Ts...>::Type;
    yasl::construct_at(reinterpret_cast<T*>(storage_), yasl::forward<U>(value));
    index_ = uint8(I);
  }

  Variant(const Variant& other) {
    if (!other.valueless()) {
      other.visit_index([&]<size_t I>() {
        using T = typename detail::TypeAt<I, Ts...>::Type;
        yasl::construct_at(reinterpret_cast<T*>(storage_),
                           *other.template get_unchecked<I>());
      });
      index_ = other.index_;
    }
  }

  Variant(Variant&& other) noexcept((NothrowMoveConstructible<Ts> && ...)) {
    if (!other.valueless()) {
      other.visit_index([&]<size_t I>() {
        using T = typename detail::TypeAt<I, Ts...>::Type;
        yasl::construct_at(reinterpret_cast<T*>(storage_),
                           yasl::move(*other.template get_unchecked<I>()));
      });
      index_ = other.index_;
    }
  }

  ~Variant() { destroy(); }

  Variant& operator=(const Variant& other) {
    if (this != &other) {
      Variant tmp(other);
      destroy();
      construct_from(yasl::move(tmp));
    }
    return *this;
  }

  Variant& operator=(Variant&& other) noexcept((NothrowMoveConstructible<Ts> &&
                                                ...)) {
    if (this != &other) {
      destroy();
      construct_from(yasl::move(other));
    }
    return *this;
  }

  template <class U>
    requires(!SameAs<RemoveCVRef<U>, Variant>)
  Variant& operator=(U&& value) {
    constexpr size_t I = detail::pick_alternative<U, Ts...>();
    using T = typename detail::TypeAt<I, Ts...>::Type;
    emplace<T>(yasl::forward<U>(value));
    return *this;
  }

  // Destroys the current value and constructs a T from args.
  template <class T, class... Args>
  T& emplace(Args&&... args) {
    static_assert(detail::kCountOf<T, Ts...> == 1,
                  "Variant::emplace: T must be exactly one alternative");
    destroy();
    T* p = yasl::construct_at(reinterpret_cast<T*>(storage_),
                              yasl::forward<Args>(args)...);
    index_ = uint8(detail::kIndexOf<T, Ts...>);
    return *p;
  }

  // Position of the held alternative in Ts... (size_t(-1) if valueless).
  size_t index() const noexcept { return valueless() ? size_t(-1) : index_; }

  bool valueless() const noexcept { return index_ == kValueless; }

  template <class T>
  bool is() const noexcept {
    static_assert(detail::kCountOf<T, Ts...> == 1);
    return index_ == detail::kIndexOf<T, Ts...>;
  }

  // Pointer to the value if the variant holds a T, otherwise nullptr (no
  // exceptions).
  template <class T>
  T* get() noexcept {
    return is<T>() ? launder_as<T>() : nullptr;
  }

  template <class T>
  const T* get() const noexcept {
    return is<T>() ? launder_as<T>() : nullptr;
  }

  // Calls f with the held value; f must return the same type for every
  // alternative.
  template <class F>
  decltype(auto) visit(F&& f) & {
    return visit_impl<0>(*this, yasl::forward<F>(f));
  }

  template <class F>
  decltype(auto) visit(F&& f) const& {
    return visit_impl<0>(*this, yasl::forward<F>(f));
  }

  template <class F>
  decltype(auto) visit(F&& f) && {
    return visit_impl<0>(yasl::move(*this), yasl::forward<F>(f));
  }

  // visit() with several lambdas glued together.
  template <class... Fs>
  decltype(auto) match(Fs&&... fs) & {
    return visit(overloaded<RemoveCVRef<Fs>...>{yasl::forward<Fs>(fs)...});
  }

  template <class... Fs>
  decltype(auto) match(Fs&&... fs) const& {
    return visit(overloaded<RemoveCVRef<Fs>...>{yasl::forward<Fs>(fs)...});
  }

  template <class... Fs>
  decltype(auto) match(Fs&&... fs) && {
    return yasl::move(*this).visit(
        overloaded<RemoveCVRef<Fs>...>{yasl::forward<Fs>(fs)...});
  }

  friend bool operator==(const Variant& a, const Variant& b)
    requires(EqualityComparable<Ts> && ...)
  {
    if (a.index_ != b.index_) {
      return false;
    }
    if (a.valueless()) {
      return true;
    }
    bool eq = false;
    a.visit_index([&]<size_t I>() {
      eq = *a.template get_unchecked<I>() == *b.template get_unchecked<I>();
    });
    return eq;
  }

 private:
  template <class T>
  T* launder_as() noexcept {
    return yasl::launder(reinterpret_cast<T*>(storage_));
  }

  template <class T>
  const T* launder_as() const noexcept {
    return yasl::launder(reinterpret_cast<const T*>(storage_));
  }

  template <size_t I>
  auto* get_unchecked() noexcept {
    return launder_as<typename detail::TypeAt<I, Ts...>::Type>();
  }

  template <size_t I>
  auto* get_unchecked() const noexcept {
    return launder_as<typename detail::TypeAt<I, Ts...>::Type>();
  }

  // Calls f.template operator()<I>() for the held index I.
  template <class F, size_t I = 0>
  void visit_index(F&& f) const {
    if constexpr (I < sizeof...(Ts)) {
      if (index_ == I) {
        f.template operator()<I>();
      } else {
        visit_index<F, I + 1>(yasl::forward<F>(f));
      }
    }
  }

  template <size_t I, class V, class F>
  static decltype(auto) visit_impl(V&& v, F&& f) {
    YASL_ASSERT(!v.valueless());
    if constexpr (I + 1 == sizeof...(Ts)) {
      return yasl::forward<F>(f)(
          forward_like<V>(*v.template get_unchecked<I>()));
    } else {
      if (v.index_ == I) {
        return yasl::forward<F>(f)(
            forward_like<V>(*v.template get_unchecked<I>()));
      }
      return visit_impl<I + 1>(yasl::forward<V>(v), yasl::forward<F>(f));
    }
  }

  // The alternative with the value category of the variant it came from.
  template <class V, class T>
  static decltype(auto) forward_like(T& x) noexcept {
    if constexpr (!SameAs<V, RemoveReference<V>&>) {
      return static_cast<T&&>(x);
    } else {
      return (x);
    }
  }

  void destroy() noexcept {
    if (!valueless()) {
      visit_index([&]<size_t I>() { yasl::destroy_at(get_unchecked<I>()); });
      index_ = kValueless;
    }
  }

  void construct_from(Variant&& other) {
    if (!other.valueless()) {
      other.visit_index([&]<size_t I>() {
        using T = typename detail::TypeAt<I, Ts...>::Type;
        yasl::construct_at(reinterpret_cast<T*>(storage_),
                           yasl::move(*other.template get_unchecked<I>()));
      });
      index_ = other.index_;
    }
  }

  alignas(Ts...) unsigned char storage_[detail::max_of<sizeof(Ts)...>()];
  uint8 index_ = kValueless;
};
}  // namespace yasl
