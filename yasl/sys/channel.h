#pragma once

#include "condvar.h"
#include "mutex.h"
#include "optional.h"
#include "result.h"
#include "utility.h"

#include <yasl/mem/resource.h>
#include <yasl/ptr/shared.h>

namespace yasl {
    namespace detail {
        template <class T>
        struct ChannelState {
            Mutex m;
            CondVar not_empty; // a value arrived, or the channel closed
            CondVar not_full;  // room appeared (buffered) / the offered value was taken (rendezvous)
            mem::Resource* res;
            size_t cap;
            bool closed = false;
            size_t senders = 1;
            size_t receivers = 1;

            // capacity > 0: ring buffer
            T* buf = nullptr;
            size_t head = 0;
            size_t count = 0;

            // capacity == 0: a single hand-off slot; tickets tell a sender its value was taken
            Optional<T> slot;
            uint64 put = 0;
            uint64 taken = 0;
            size_t waiting_receivers = 0;

            ChannelState(mem::Resource* r, size_t capacity)
                : res(r)
                , cap(capacity)
            {
                if (cap > 0) {
                    buf = res->template allocate_array<T>(cap);
                }
            }

            ~ChannelState() {
                for (size_t i = 0; i < count; ++i) {
                    yasl::destroy_at(buf + (head + i) % cap);
                }
                if (buf) {
                    res->deallocate_array(buf, cap);
                }
            }

            void push(T&& v) {
                yasl::construct_at(buf + (head + count) % cap, yasl::move(v));
                ++count;
            }

            T pop() {
                T* p = buf + head;
                T v = yasl::move(*p);
                yasl::destroy_at(p);
                head = (head + 1) % cap;
                --count;
                return v;
            }

            void close_locked() {
                closed = true;
                not_empty.notify_all();
                not_full.notify_all();
            }
        };
    }

    template <class T>
    class Receiver;

    // Sending half of a channel. Copies share the channel (many producers). When the last
    // Sender is dropped the channel closes.
    template <class T>
    class Sender {
        using State = detail::ChannelState<T>;

    public:
        Sender() noexcept = default;

        Sender(const Sender& other)
            : s_(other.s_)
        {
            if (s_) {
                LockGuard<Mutex> g(s_->m);
                ++s_->senders;
            }
        }

        Sender(Sender&& other) noexcept = default;

        Sender& operator=(Sender other) noexcept {
            s_.swap(other.s_);
            return *this;
        }

        ~Sender() {
            drop();
        }

        // Blocks while the buffer is full (or, with capacity 0, until a receiver takes the value).
        // Gives the value back if the channel is closed or has no receivers left.
        Result<void, T> send(T v) {
            State& s = *s_;
            LockGuard<Mutex> g(s.m);
            if (s.cap > 0) {
                while (s.count == s.cap && !s.closed && s.receivers > 0) {
                    s.not_full.wait(s.m);
                }
                if (s.closed || s.receivers == 0) {
                    return err(yasl::move(v));
                }
                s.push(yasl::move(v));
                s.not_empty.notify_one();
                return ok();
            }
            while (s.slot.has_value() && !s.closed && s.receivers > 0) {
                s.not_full.wait(s.m);
            }
            if (s.closed || s.receivers == 0) {
                return err(yasl::move(v));
            }
            s.slot.emplace(yasl::move(v));
            uint64 ticket = ++s.put;
            s.not_empty.notify_one();
            while (s.taken < ticket) {
                if (s.receivers == 0) {
                    // nobody left to take it
                    return err(s.slot.take().value());
                }
                s.not_full.wait(s.m);
            }
            return ok();
        }

        // Never blocks. Fails (returning the value) if the buffer is full, the channel is closed, or
        // - with capacity 0 - no receiver is waiting.
        Result<void, T> try_send(T v) {
            State& s = *s_;
            LockGuard<Mutex> g(s.m);
            if (s.closed || s.receivers == 0) {
                return err(yasl::move(v));
            }
            if (s.cap > 0) {
                if (s.count == s.cap) {
                    return err(yasl::move(v));
                }
                s.push(yasl::move(v));
                s.not_empty.notify_one();
                return ok();
            }
            if (s.slot.has_value() || s.waiting_receivers == 0) {
                return err(yasl::move(v));
            }
            s.slot.emplace(yasl::move(v));
            ++s.put;
            s.not_empty.notify_one();
            return ok();
        }

        // Receivers see the remaining values, then none.
        void close() {
            LockGuard<Mutex> g(s_->m);
            s_->close_locked();
        }

        bool is_closed() const {
            LockGuard<Mutex> g(s_->m);
            return s_->closed;
        }

    private:
        template <class U>
        friend Pair<Sender<U>, Receiver<U>> make_channel(size_t, mem::Resource*);

        explicit Sender(Shared<State> s) noexcept
            : s_(yasl::move(s))
        {
        }

        void drop() {
            if (s_) {
                LockGuard<Mutex> g(s_->m);
                if (--s_->senders == 0) {
                    s_->close_locked();
                }
            }
        }

        Shared<State> s_;
    };

    // Receiving half of a channel; copies share it (many consumers).
    //
    //     auto [tx, rx] = make_channel<int>(8);
    //     Thread producer([tx] { for (int i = 0; i < 100; ++i) tx.send(i); });
    //     for (int v : rx) use(v);          // ends when every Sender is gone or closed
    template <class T>
    class Receiver {
        using State = detail::ChannelState<T>;

    public:
        Receiver() noexcept = default;

        Receiver(const Receiver& other)
            : s_(other.s_)
        {
            if (s_) {
                LockGuard<Mutex> g(s_->m);
                ++s_->receivers;
            }
        }

        Receiver(Receiver&& other) noexcept = default;

        Receiver& operator=(Receiver other) noexcept {
            s_.swap(other.s_);
            return *this;
        }

        ~Receiver() {
            if (s_) {
                LockGuard<Mutex> g(s_->m);
                if (--s_->receivers == 0) {
                    s_->not_full.notify_all();
                }
            }
        }

        // Blocks for the next value; none once the channel is closed and drained.
        Optional<T> recv() {
            State& s = *s_;
            LockGuard<Mutex> g(s.m);
            if (s.cap > 0) {
                while (s.count == 0 && !s.closed) {
                    s.not_empty.wait(s.m);
                }
                if (s.count == 0) {
                    return none;
                }
                Optional<T> v(s.pop());
                s.not_full.notify_one();
                return v;
            }
            ++s.waiting_receivers;
            while (!s.slot.has_value() && !s.closed) {
                s.not_empty.wait(s.m);
            }
            --s.waiting_receivers;
            if (!s.slot.has_value()) {
                return none;
            }
            Optional<T> v = s.slot.take();
            ++s.taken;
            s.not_full.notify_all();
            return v;
        }

        Optional<T> try_recv() {
            State& s = *s_;
            LockGuard<Mutex> g(s.m);
            if (s.cap > 0) {
                if (s.count == 0) {
                    return none;
                }
                Optional<T> v(s.pop());
                s.not_full.notify_one();
                return v;
            }
            if (!s.slot.has_value()) {
                return none;
            }
            Optional<T> v = s.slot.take();
            ++s.taken;
            s.not_full.notify_all();
            return v;
        }

        // Like recv() but gives up after `timeout`.
        Optional<T> recv_for(Duration timeout) {
            State& s = *s_;
            LockGuard<Mutex> g(s.m);
            Instant deadline = Instant::now() + timeout;
            if (s.cap > 0) {
                while (s.count == 0 && !s.closed) {
                    Duration left = deadline - Instant::now();
                    if (left <= Duration() || !s.not_empty.wait_for(s.m, left)) {
                        break;
                    }
                }
                if (s.count == 0) {
                    return none;
                }
                Optional<T> v(s.pop());
                s.not_full.notify_one();
                return v;
            }
            ++s.waiting_receivers;
            while (!s.slot.has_value() && !s.closed) {
                Duration left = deadline - Instant::now();
                if (left <= Duration() || !s.not_empty.wait_for(s.m, left)) {
                    break;
                }
            }
            --s.waiting_receivers;
            if (!s.slot.has_value()) {
                return none;
            }
            Optional<T> v = s.slot.take();
            ++s.taken;
            s.not_full.notify_all();
            return v;
        }

        void close() {
            LockGuard<Mutex> g(s_->m);
            s_->close_locked();
        }

        // Input iteration: each ++ takes the next value.
        class Iter {
        public:
            struct Sentinel {};

            T& operator*() noexcept {
                return *cur_;
            }

            T* operator->() noexcept {
                return &*cur_;
            }

            Iter& operator++() {
                cur_ = rx_->recv();
                return *this;
            }

            friend bool operator==(const Iter& it, Sentinel) noexcept {
                return !it.cur_.has_value();
            }

        private:
            friend class Receiver;

            explicit Iter(Receiver* rx)
                : rx_(rx)
                , cur_(rx->recv())
            {
            }

            Receiver* rx_;
            Optional<T> cur_;
        };

        Iter begin() {
            return Iter(this);
        }

        typename Iter::Sentinel end() noexcept {
            return {};
        }

    private:
        template <class U>
        friend Pair<Sender<U>, Receiver<U>> make_channel(size_t, mem::Resource*);

        explicit Receiver(Shared<State> s) noexcept
            : s_(yasl::move(s))
        {
        }

        Shared<State> s_;
    };

    // A channel between threads (Go chan). capacity > 0 buffers that many values and send blocks
    // when full; capacity 0 is a rendezvous: send waits until a receiver takes the value.
    template <class T>
    Pair<Sender<T>, Receiver<T>> make_channel(size_t capacity = 0, mem::Resource* res = mem::default_resource()) {
        auto state = make_shared_in<detail::ChannelState<T>>(res, res, capacity);
        return {Sender<T>(state), Receiver<T>(state)};
    }
}
