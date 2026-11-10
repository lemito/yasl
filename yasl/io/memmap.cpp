#include "memmap.h"

#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file.h"

namespace yasl::io {
MemMap::MemMap(StringView path, MemMapMode mode, uint64 offset, uint64 length) {
  const auto open_mode = mode == MemMapMode::ReadWrite
                             ? OpenMode::Read | OpenMode::Write
                             : OpenMode::Read;
  File file = File::open(path, open_mode);
  *this = map(file.fd(), mode, offset, length);
}

MemMap MemMap::map(int fd, MemMapMode mode, uint64 offset, uint64 length) {
  if (mode != MemMapMode::ReadOnly && mode != MemMapMode::ReadWrite &&
      mode != MemMapMode::CopyOnWrite) {
    throw Error("mmap mode", "fd", EINVAL);
  }
  struct stat st;
  if (::fstat(fd, &st) != 0) {
    throw Error("fstat", "fd", errno);
  }
  if (!S_ISREG(st.st_mode) || st.st_size < 0) {
    throw Error("mmap file type", "fd", EINVAL);
  }
  const uint64 file_size = uint64(st.st_size);
  if (offset > file_size) {
    throw OutOfRange("mmap: offset past end of file");
  }
  const uint64 available = file_size - offset;
  if (length == kToEnd) {
    length = available;
  }
  if (length > available) {
    throw OutOfRange("mmap: range past end of file");
  }
  MemMap result;
  result.mode_ = mode;
  if (!length) {
    return result;
  }
  const long page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    throw Error("sysconf page size", "fd", EINVAL);
  }
  const uint64 head = offset % uint64(page_size);
  if (length > uint64(size_t(-1)) - head) {
    throw LengthError("mmap: range too large for address space");
  }
  const size_t mapped_size = size_t(length + head);
  const int prot = PROT_READ | (mode == MemMapMode::ReadOnly ? 0 : PROT_WRITE);
  void* base =
      ::mmap(nullptr, mapped_size, prot,
             mode == MemMapMode::CopyOnWrite ? MAP_PRIVATE : MAP_SHARED, fd,
             off_t(offset - head));
  if (base == MAP_FAILED) {
    throw Error("mmap", "fd", errno);
  }
  result.base_ = base;
  result.mapped_size_ = mapped_size;
  result.head_ = size_t(head);
  result.size_ = size_t(length);
  return result;
}

void MemMap::swap(MemMap& other) noexcept {
  yasl::swap(base_, other.base_);
  yasl::swap(mapped_size_, other.mapped_size_);
  yasl::swap(head_, other.head_);
  yasl::swap(size_, other.size_);
  yasl::swap(mode_, other.mode_);
}

MemMap::MemMap(MemMap&& other) noexcept { swap(other); }

MemMap& MemMap::operator=(MemMap&& other) noexcept {
  if (this != &other) {
    MemMap replacement(yasl::move(other));
    swap(replacement);
  }
  return *this;
}

MemMap::~MemMap() { close_quietly(); }

char* MemMap::mut_data() {
  if (mode_ == MemMapMode::ReadOnly) {
    throw BadAccess("mmap: read-only mapping");
  }
  return mapped_size_ ? static_cast<char*>(base_) + head_ : nullptr;
}

void MemMap::flush() {
  if (mapped_size_ && mode_ == MemMapMode::ReadWrite &&
      ::msync(base_, mapped_size_, MS_SYNC) != 0) {
    throw Error("msync", "mapping", errno);
  }
}

void MemMap::close() {
  if (mapped_size_ && ::munmap(base_, mapped_size_) != 0) {
    throw Error("munmap", "mapping", errno);
  }
  base_ = nullptr;
  mapped_size_ = head_ = size_ = 0;
  mode_ = MemMapMode::ReadOnly;
}

void MemMap::close_quietly() noexcept {
  if (mapped_size_) {
    ::munmap(base_, mapped_size_);
  }
}
}  // namespace yasl::io
