#pragma once

// Interop layer: this header is allowed to use the standard library.
// Nothing in yasl includes it; include it yourself where yasl meets std code.

#include <yasl/ptr/shared.h>
#include <yasl/ptr/unique.h>
#include <yasl/str/builder.h>
#include <yasl/str/str.h>
#include <yasl/str/view.h>

#include <format>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>

namespace yasl {
    // std::string and std::string_view already convert to StringView implicitly.

    inline std::string_view to_std_view(StringView v) noexcept {
        return {v.data(), v.size()};
    }

    inline std::string to_std_string(StringView v) {
        return {v.data(), v.size()};
    }

    inline std::ostream& operator<<(std::ostream& os, StringView v) {
        return os << to_std_view(v);
    }

    inline std::ostream& operator<<(std::ostream& os, const String& s) {
        return os << to_std_view(s.view());
    }

    // Lets yasl::io::Output print std strings without a copy.
    inline io::Output& operator<<(io::Output& out, const std::string& s) {
        return out << StringView(s);
    }
}

template <>
struct std::hash<yasl::StringView> {
    size_t operator()(yasl::StringView v) const noexcept {
        return hash_value(v);
    }
};

template <>
struct std::hash<yasl::String> {
    size_t operator()(const yasl::String& s) const noexcept {
        return hash_value(s);
    }
};

// Hash of the pointer, like std::unique_ptr / std::shared_ptr. There is deliberately no
// conversion of Unique to std::unique_ptr: the allocators differ.
template <class T>
struct std::hash<yasl::Unique<T>> {
    size_t operator()(const yasl::Unique<T>& p) const noexcept {
        return std::hash<T*>()(p.get());
    }
};

template <class T>
struct std::hash<yasl::Shared<T>> {
    size_t operator()(const yasl::Shared<T>& p) const noexcept {
        return std::hash<T*>()(p.get());
    }
};

template <>
struct std::formatter<yasl::StringView, char>: std::formatter<std::string_view, char> {
    auto format(yasl::StringView v, std::format_context& ctx) const {
        return std::formatter<std::string_view, char>::format(yasl::to_std_view(v), ctx);
    }
};

template <>
struct std::formatter<yasl::String, char>: std::formatter<std::string_view, char> {
    auto format(const yasl::String& s, std::format_context& ctx) const {
        return std::formatter<std::string_view, char>::format(yasl::to_std_view(s.view()), ctx);
    }
};
