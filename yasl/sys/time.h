#pragma once

#include "utility.h"

#include <yasl/io/output.h>

namespace yasl {
    // A span of time in nanoseconds (int64: about +-292 years), like Go's time.Duration.
    //
    //     using namespace yasl::literals;
    //     Duration timeout = 250_ms + 2_s;
    //     io::println("{}", timeout);   // 2.25s
    class Duration {
    public:
        constexpr Duration() noexcept = default;

        static constexpr Duration nanos(int64 n) noexcept {
            return Duration(n);
        }

        static constexpr Duration micros(int64 n) noexcept {
            return Duration(n * 1'000);
        }

        static constexpr Duration millis(int64 n) noexcept {
            return Duration(n * 1'000'000);
        }

        static constexpr Duration secs(int64 n) noexcept {
            return Duration(n * 1'000'000'000);
        }

        static constexpr Duration mins(int64 n) noexcept {
            return secs(n * 60);
        }

        static constexpr Duration hours(int64 n) noexcept {
            return secs(n * 3600);
        }

        static constexpr Duration from_secs_f64(double s) noexcept {
            return Duration(int64(s * 1e9));
        }

        static constexpr Duration max() noexcept {
            return Duration(max_value<int64>());
        }

        constexpr int64 as_nanos() const noexcept {
            return ns_;
        }

        constexpr int64 as_micros() const noexcept {
            return ns_ / 1'000;
        }

        constexpr int64 as_millis() const noexcept {
            return ns_ / 1'000'000;
        }

        constexpr int64 as_secs() const noexcept {
            return ns_ / 1'000'000'000;
        }

        constexpr double as_secs_f64() const noexcept {
            return double(ns_) / 1e9;
        }

        constexpr bool is_zero() const noexcept {
            return ns_ == 0;
        }

        friend constexpr Duration operator+(Duration a, Duration b) noexcept {
            return Duration(a.ns_ + b.ns_);
        }

        friend constexpr Duration operator-(Duration a, Duration b) noexcept {
            return Duration(a.ns_ - b.ns_);
        }

        friend constexpr Duration operator-(Duration a) noexcept {
            return Duration(-a.ns_);
        }

        friend constexpr Duration operator*(Duration a, int64 k) noexcept {
            return Duration(a.ns_ * k);
        }

        friend constexpr Duration operator*(int64 k, Duration a) noexcept {
            return Duration(a.ns_ * k);
        }

        friend constexpr Duration operator/(Duration a, int64 k) noexcept {
            return Duration(a.ns_ / k);
        }

        friend constexpr int64 operator/(Duration a, Duration b) noexcept {
            return a.ns_ / b.ns_;
        }

        constexpr Duration& operator+=(Duration o) noexcept {
            ns_ += o.ns_;
            return *this;
        }

        constexpr Duration& operator-=(Duration o) noexcept {
            ns_ -= o.ns_;
            return *this;
        }

        friend constexpr bool operator==(Duration, Duration) noexcept = default;
        friend constexpr auto operator<=>(Duration, Duration) noexcept = default;

    private:
        explicit constexpr Duration(int64 ns) noexcept
            : ns_(ns)
        {
        }

        int64 ns_ = 0;
    };

    // Go style: 0s, 250ms, 1.5s, 1m30s, 72h3m0.5s, 1.5us, 100ns; a leading '-' for negatives.
    io::Output& operator<<(io::Output& out, Duration d);

    // Monotonic clock for measuring intervals; unrelated to the wall clock.
    class Instant {
    public:
        constexpr Instant() noexcept = default;

        static Instant now() noexcept;

        Duration elapsed() const noexcept {
            return now() - *this;
        }

        friend constexpr Duration operator-(Instant a, Instant b) noexcept {
            return Duration::nanos(a.ns_ - b.ns_);
        }

        friend constexpr Instant operator+(Instant a, Duration d) noexcept {
            return Instant(a.ns_ + d.as_nanos());
        }

        friend constexpr Instant operator-(Instant a, Duration d) noexcept {
            return Instant(a.ns_ - d.as_nanos());
        }

        friend constexpr bool operator==(Instant, Instant) noexcept = default;
        friend constexpr auto operator<=>(Instant, Instant) noexcept = default;

    private:
        explicit constexpr Instant(int64 ns) noexcept
            : ns_(ns)
        {
        }

        int64 ns_ = 0;
    };

    // Wall-clock time (can jump); nanoseconds since the Unix epoch.
    class SystemTime {
    public:
        constexpr SystemTime() noexcept = default;

        static SystemTime now() noexcept;

        static constexpr SystemTime from_unix_nanos(int64 ns) noexcept {
            return SystemTime(ns);
        }

        constexpr int64 unix_nanos() const noexcept {
            return ns_;
        }

        constexpr int64 unix_secs() const noexcept {
            return ns_ / 1'000'000'000;
        }

        friend constexpr Duration operator-(SystemTime a, SystemTime b) noexcept {
            return Duration::nanos(a.ns_ - b.ns_);
        }

        friend constexpr SystemTime operator+(SystemTime a, Duration d) noexcept {
            return SystemTime(a.ns_ + d.as_nanos());
        }

        friend constexpr bool operator==(SystemTime, SystemTime) noexcept = default;
        friend constexpr auto operator<=>(SystemTime, SystemTime) noexcept = default;

    private:
        explicit constexpr SystemTime(int64 ns) noexcept
            : ns_(ns)
        {
        }

        int64 ns_ = 0;
    };

    namespace literals {
        constexpr Duration operator""_ns(unsigned long long n) noexcept {
            return Duration::nanos(int64(n));
        }

        constexpr Duration operator""_us(unsigned long long n) noexcept {
            return Duration::micros(int64(n));
        }

        constexpr Duration operator""_ms(unsigned long long n) noexcept {
            return Duration::millis(int64(n));
        }

        constexpr Duration operator""_s(unsigned long long n) noexcept {
            return Duration::secs(int64(n));
        }

        constexpr Duration operator""_min(unsigned long long n) noexcept {
            return Duration::mins(int64(n));
        }

        constexpr Duration operator""_h(unsigned long long n) noexcept {
            return Duration::hours(int64(n));
        }
    }
}
