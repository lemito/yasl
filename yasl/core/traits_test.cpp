#include <yasl/core/traits.h>
#include <yasl/cont/seq/vector.h>
#include <yasl/test/test.h>

using namespace yasl;

YASL_TEST(traits_remove_reference) {
  static_assert(SameAs<RemoveReference<int&>, int>);
  static_assert(SameAs<RemoveReference<int&&>, int>);
  static_assert(SameAs<RemoveReference<int>, int>);
  CHECK(true);
}

YASL_TEST(traits_remove_cv) {
  static_assert(SameAs<RemoveCV<const int>, int>);
  static_assert(SameAs<RemoveCV<volatile int>, int>);
  static_assert(SameAs<RemoveCV<const volatile int>, int>);
  static_assert(SameAs<RemoveCV<int>, int>);
  CHECK(true);
}

YASL_TEST(traits_remove_cvref) {
  static_assert(SameAs<RemoveCVRef<const int&>, int>);
  static_assert(SameAs<RemoveCVRef<volatile int&&>, int>);
  CHECK(true);
}

YASL_TEST(concepts_integral) {
  static_assert(Integral<int>);
  static_assert(Integral<unsigned long long>);
  static_assert(Integral<char>);
  static_assert(!Integral<double>);
  static_assert(!Integral<String>);
  CHECK(true);
}

YASL_TEST(concepts_signed_unsigned) {
  static_assert(SignedIntegral<int>);
  static_assert(SignedIntegral<long long>);
  static_assert(!SignedIntegral<unsigned int>);
  static_assert(UnsignedIntegral<unsigned int>);
  static_assert(UnsignedIntegral<size_t>);
  static_assert(!UnsignedIntegral<int>);
  CHECK(true);
}

YASL_TEST(concepts_floating_point) {
  static_assert(FloatingPoint<float>);
  static_assert(FloatingPoint<double>);
  static_assert(!FloatingPoint<int>);
  CHECK(true);
}

YASL_TEST(concepts_arithmetic) {
  static_assert(Arithmetic<int>);
  static_assert(Arithmetic<double>);
  static_assert(!Arithmetic<String>);
  CHECK(true);
}

YASL_TEST(concepts_pointer) {
  static_assert(Pointer<int*>);
  static_assert(Pointer<const char*>);
  static_assert(!Pointer<int>);
  CHECK(true);
}

YASL_TEST(concepts_convertible_to) {
  struct ExplicitInt {
    explicit ExplicitInt(int) {}
  };
  static_assert(ConvertibleTo<int, long>);
  static_assert(!ConvertibleTo<int, ExplicitInt>);
  static_assert(ConvertibleTo<const char*, StringView>);
  CHECK(true);
}

YASL_TEST(concepts_constructible) {
  static_assert(ConstructibleFrom<String, const char*>);
  static_assert(ConstructibleFrom<Vector<int>>);
  static_assert(DefaultConstructible<Vector<int>>);
  CHECK(true);
}

YASL_TEST(concepts_move_copy) {
  static_assert(MoveConstructible<Vector<int>>);
  static_assert(CopyConstructible<Vector<int>>);
  static_assert(NothrowMoveConstructible<int>);
  CHECK(true);
}

YASL_TEST(concepts_equality_and_comparison) {
  static_assert(EqualityComparable<int>);
  static_assert(EqualityComparableWith<int, long>);
  static_assert(ThreeWayComparable<int>);
  static_assert(ThreeWayComparable<String>);
  CHECK(true);
}

YASL_TEST(trivially_relocatable_default) {
  static_assert(IsTriviallyRelocatable<int>);
  static_assert(IsTriviallyRelocatable<double>);
  static_assert(IsTriviallyRelocatable<char*>);
  CHECK(true);
}

YASL_TEST(trivially_relocatable_string_specialised) {
  // String is COW and specially marked trivially relocatable.
  static_assert(IsTriviallyRelocatable<String>);
  CHECK(true);
}

YASL_TEST(trivially_destructible_concept) {
  static_assert(TriviallyDestructible<int>);
  static_assert(!TriviallyDestructible<String>);
  CHECK(true);
}

YASL_TEST(has_virtual_destructor) {
  struct Base {
    virtual ~Base() = default;
  };
  struct Derived : Base {};
  struct Plain {};
  static_assert(HasVirtualDestructor<Base>);
  static_assert(HasVirtualDestructor<Derived>);
  static_assert(!HasVirtualDestructor<Plain>);
  CHECK(true);
}

YASL_TEST(derived_from) {
  struct Base {};
  struct Derived : Base {};
  static_assert(DerivedFrom<Derived, Base>);
  static_assert(!DerivedFrom<Base, Derived>);
  CHECK(true);
}
