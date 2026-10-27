#include "buffer.h"

namespace yasl::io {
void Buffer::reserve(size_t extra) {
  if (extra <= cap_ - end_) {
    return;
  }
  size_t used = size();
  if (extra > max_value<size_t>() - used) {
    throw BadAlloc();
  }
  // Enough room once the consumed prefix is reclaimed?
  if (data_ && extra <= cap_ - used && begin_ >= used) {
    yasl::mem_copy(data_, data_ + begin_,
                   used);  // non-overlapping: begin_ >= used
    begin_ = 0;
    end_ = used;
    return;
  }
  size_t want = used + extra;
  size_t cap = cap_ ? cap_ : 64;
  while (cap < want) {
    cap = cap > max_value<size_t>() / 2 ? want : cap * 2;
  }
  mem::Block b = res_->allocate_at_least(cap, 1);
  char* fresh = static_cast<char*>(b.ptr);
  if (used) {
    yasl::mem_copy(fresh, data_ + begin_, used);
  }
  free_storage();
  data_ = fresh;
  cap_ = b.size;
  begin_ = 0;
  end_ = used;
}
}  // namespace yasl::io
