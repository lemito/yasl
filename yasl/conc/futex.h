#pragma once

#include <yasl/core/types.h>
#include <yasl/time/time.h>

#include <atomic>

namespace yasl::detail {
void futex_wait(std::atomic<uint32>& word, uint32 expected) noexcept;
bool futex_wait_for(std::atomic<uint32>& word, uint32 expected,
                    Duration timeout) noexcept;
void futex_wake_one(std::atomic<uint32>& word) noexcept;
void futex_wake_all(std::atomic<uint32>& word) noexcept;
}  // namespace yasl::detail
