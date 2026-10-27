#include "input.h"

namespace yasl::io {
    namespace {
        bool is_space(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }
    }

    size_t Input::read(char* dst, size_t n) {
        size_t done = 0;
        while (done < n) {
            if (pos_ == end_ && !refill()) {
                break;
            }
            size_t step = yasl::min(n - done, size_t(end_ - pos_));
            yasl::mem_copy(dst + done, pos_, step);
            pos_ += step;
            done += step;
        }
        return done;
    }

    bool Input::read_line(String& line) {
        line.clear();
        bool any = false;
        for (;;) {
            if (pos_ == end_ && !refill()) {
                break;
            }
            any = true;
            StringView chunk(pos_, size_t(end_ - pos_));
            size_t nl = chunk.find('\n');
            if (nl != StringView::npos) {
                line.append(chunk.first(nl));
                pos_ += nl + 1;
                if (line.ends_with("\r")) {
                    line.pop_back();
                }
                return true;
            }
            line.append(chunk);
            pos_ = end_;
        }
        if (line.ends_with("\r")) {
            line.pop_back();
        }
        return any;
    }

    void Input::skip_whitespace() {
        for (;;) {
            if (pos_ == end_ && !refill()) {
                return;
            }
            while (pos_ != end_ && is_space(*pos_)) {
                ++pos_;
            }
            if (pos_ != end_) {
                return;
            }
        }
    }

    bool Input::read_word(String& word) {
        word.clear();
        skip_whitespace();
        bool any = false;
        for (;;) {
            if (pos_ == end_ && !refill()) {
                break;
            }
            const char* start = pos_;
            while (pos_ != end_ && !is_space(*pos_)) {
                ++pos_;
            }
            word.append(StringView(start, size_t(pos_ - start)));
            any |= pos_ != start;
            if (pos_ != end_) {
                break;
            }
        }
        return any;
    }

    bool Input::read_token(char* buf, size_t cap, size_t& n) {
        n = 0;
        skip_whitespace();
        bool fits = true;
        for (;;) {
            if (pos_ == end_ && !refill()) {
                break;
            }
            while (pos_ != end_ && !is_space(*pos_)) {
                if (n < cap) {
                    buf[n++] = *pos_;
                } else {
                    fits = false;
                }
                ++pos_;
            }
            if (pos_ != end_) {
                break;
            }
        }
        return n > 0 && fits;
    }

    String Input::read_all() {
        String all;
        for (;;) {
            if (pos_ == end_ && !refill()) {
                break;
            }
            all.append(StringView(pos_, size_t(end_ - pos_)));
            pos_ = end_;
        }
        return all;
    }
}
