#pragma once

namespace yasl::NoneCopy {
struct SNoneCopyable {
  SNoneCopyable() = default;
  ~SNoneCopyable() = default;

  SNoneCopyable(const SNoneCopyable&) = delete;
  SNoneCopyable& operator=(const SNoneCopyable&) = delete;
};
struct SCopyOnly {
  SCopyOnly() = default;
  ~SCopyOnly() = default;

  SCopyOnly(const SCopyOnly&) = default;
  SCopyOnly& operator=(const SCopyOnly&) = default;

  SCopyOnly(SCopyOnly&&) noexcept = delete;
  SCopyOnly& operator=(SCopyOnly&&) noexcept = delete;
};
struct SMoveOnly {
  SMoveOnly() = default;
  ~SMoveOnly() = default;

  SMoveOnly(const SMoveOnly&) = delete;
  SMoveOnly& operator=(const SMoveOnly&) = delete;

  SMoveOnly(SMoveOnly&&) noexcept = default;
  SMoveOnly& operator=(SMoveOnly&&) noexcept = default;
};
struct SNoMoveNoCopy {
  SNoMoveNoCopy() = default;
  ~SNoMoveNoCopy() = default;

  SNoMoveNoCopy(const SNoMoveNoCopy&) = delete;
  SNoMoveNoCopy& operator=(const SNoMoveNoCopy&) = delete;

  SNoMoveNoCopy(SNoMoveNoCopy&&) noexcept = delete;
  SNoMoveNoCopy& operator=(SNoMoveNoCopy&&) noexcept = delete;
};
}  // namespace yasl::NoneCopy

using NoneCopyable = yasl::NoneCopy::SNoneCopyable;
using CopyOnly = yasl::NoneCopy::SCopyOnly;
using MoveOnly = yasl::NoneCopy::SMoveOnly;
using NoMoveNoCopy = yasl::NoneCopy::SNoMoveNoCopy;