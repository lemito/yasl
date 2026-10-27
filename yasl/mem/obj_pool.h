#pragma once

#include <yasl/str/view.h>

#include "arena.h"

namespace yasl::mem {
// Owner of a group of objects that live side by side in an Arena and die
// together: the destructors run in reverse order of creation, then the memory
// goes back upstream. Objects with a trivial destructor cost nothing extra. Not
// thread-safe.
//
// An ObjPool is also a Resource, so containers can take their memory from it.
//
//     ObjPool pool;
//     Node* a = pool.make<Node>(1);
//     Node* b = pool.make<Node>(2, a);
//     StringView name = pool.copy_str("hello");
//     // ~ObjPool(): b is destroyed first, then a
class ObjPool final : public Resource {
 public:
  explicit ObjPool(Resource* upstream = default_resource(),
                   size_t first_chunk = 4096) noexcept
      : arena_(upstream, first_chunk) {}

  ObjPool(const ObjPool&) = delete;
  ObjPool& operator=(const ObjPool&) = delete;

  ~ObjPool() override { release(); }

  // Constructs a T in the pool. If the constructor throws, nothing is
  // registered.
  template <class T, class... Args>
  T* make(Args&&... args) {
    return construct<T>(
        1, [&](T* p, size_t) { ::new (p) T(yasl::forward<Args>(args)...); });
  }

  // n default-constructed objects in a row.
  template <class T>
  T* make_array(size_t n) {
    return construct<T>(n, [](T* p, size_t i) { ::new (p + i) T(); });
  }

  // A NUL-terminated copy of s that lives as long as the pool.
  StringView copy_str(StringView s);

  // Destroys every object (newest first) and returns all memory upstream.
  void release() noexcept {
    run_destructors();
    arena_.release();
  }

  // Destroys every object (newest first) but keeps one chunk for reuse.
  void reset() noexcept {
    run_destructors();
    arena_.reset();
  }

  // Number of objects with a non-trivial destructor that are waiting for
  // destruction.
  size_t tracked() const noexcept { return tracked_; }

  const Arena& arena() const noexcept { return arena_; }

 protected:
  Block do_alloc(size_t size, size_t align) override {
    return arena_.allocate_at_least(size, align);
  }

  void do_free(void* ptr, size_t size, size_t align) noexcept override {
    arena_.deallocate(ptr, size, align);
  }

 private:
  // Sits in front of the objects of every group with a non-trivial destructor.
  struct Header {
    void (*destroy)(void* first, size_t n) noexcept;
    void* first;
    size_t n;
    Header* next;
  };

  template <class T, class Init>
  T* construct(size_t n, Init init) {
    if (n > max_value<size_t>() / sizeof(T)) {
      throw BadAlloc();
    }
    if constexpr (TriviallyDestructible<T>) {
      if (n == 0) {
        return nullptr;
      }
      T* p = static_cast<T*>(arena_.allocate(n * sizeof(T), alignof(T)));
      // Nothing to undo here: a trivially destructible T needs no cleanup, and
      // the arena frees the bytes anyway.
      for (size_t i = 0; i < n; ++i) {
        init(p, i);
      }
      return p;
    } else {
      constexpr size_t offset = align_up(sizeof(Header), alignof(T));
      constexpr size_t align =
          alignof(T) > alignof(Header) ? alignof(T) : alignof(Header);
      if (n > (max_value<size_t>() - offset) / sizeof(T)) {
        throw BadAlloc();
      }
      size_t bytes = offset + n * sizeof(T);
      char* raw = static_cast<char*>(arena_.allocate(bytes, align));
      T* first = reinterpret_cast<T*>(raw + offset);
      size_t done = 0;
      try {
        for (; done < n; ++done) {
          init(first, done);
        }
      } catch (...) {
        while (done > 0) {
          first[--done].~T();
        }
        arena_.deallocate(raw, bytes, align);
        throw;
      }
      head_ = ::new (raw) Header{&destroy_group<T>, first, n, head_};
      ++tracked_;
      return first;
    }
  }

  template <class T>
  static void destroy_group(void* first, size_t n) noexcept {
    T* p = static_cast<T*>(first);
    while (n > 0) {
      p[--n].~T();
    }
  }

  void run_destructors() noexcept {
    while (head_) {
      Header* h = head_;
      head_ = h->next;
      h->destroy(h->first, h->n);
    }
    tracked_ = 0;
  }

  Arena arena_;
  Header* head_ = nullptr;
  size_t tracked_ = 0;
};
}  // namespace yasl::mem
