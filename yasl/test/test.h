#pragma once

#include <yasl/str/builder.h>
#include <yasl/str/str.h>
#include <yasl/sys/optional.h>

// Minimal self-registering test framework built on yasl itself.
//
//     YASL_TEST(vector_push) {
//         Vector<int> v;
//         v.push_back(1);
//         CHECK_EQ(v.size(), 1u);
//     }

namespace yasl::test {
    using Fn = void (*)();

    void register_test(const char* name, Fn fn);
    [[noreturn]] void fail(const char* file, int line, StringView message);
    int run(int argc, char** argv);

    struct Registrar {
        Registrar(const char* name, Fn fn) {
            register_test(name, fn);
        }
    };

    template <class T>
    String show(const T& v) {
        if constexpr (requires(io::Output& o, const T& x) { o << x; }) {
            StringBuilder b;
            b << v;
            return b.take();
        } else {
            return String("<?>");
        }
    }

    template <class A, class B>
    Optional<String> eq_failure(const A& a, const B& b) {
        if (a == b) {
            return none;
        }
        return format("{} != {}", show(a), show(b));
    }
}

#define YASL_TEST(name)                                                                 \
    static void yasl_test_##name();                                                     \
    static ::yasl::test::Registrar yasl_test_reg_##name(#name, &yasl_test_##name);      \
    static void yasl_test_##name()

#define CHECK(expr)                                                                     \
    do {                                                                                \
        if (!(expr)) {                                                                  \
            ::yasl::test::fail(__FILE__, __LINE__, "CHECK(" #expr ")");                 \
        }                                                                               \
    } while (0)

// Both sides are evaluated and compared within one full-expression,
// so temporaries such as opt->member stay alive for the comparison.
#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        if (auto yasl_msg_ = ::yasl::test::eq_failure((a), (b))) {                      \
            ::yasl::test::fail(__FILE__, __LINE__,                                      \
                ::yasl::format("CHECK_EQ({}, {}): {}", ::yasl::StringView(#a),          \
                    ::yasl::StringView(#b), *yasl_msg_));                               \
        }                                                                               \
    } while (0)

#define CHECK_THROWS(expr, Ex)                                                          \
    do {                                                                                \
        bool yasl_thrown_ = false;                                                      \
        try {                                                                           \
            (void)(expr);                                                               \
        } catch (const Ex&) {                                                           \
            yasl_thrown_ = true;                                                        \
        }                                                                               \
        if (!yasl_thrown_) {                                                            \
            ::yasl::test::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ", " #Ex ")"); \
        }                                                                               \
    } while (0)
