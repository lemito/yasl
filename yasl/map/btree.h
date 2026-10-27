#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/check.h>
#include <yasl/sys/utility.h>

#include <initializer_list> // language support for braced lists

namespace yasl {
    // Ordered container on a B-tree: elements live in arrays inside the nodes, so
    // lookups and iteration are cache-friendly and per-element overhead is tiny.
    //
    // Trade-off vs Treap: elements move between nodes on insert/erase, so ANY
    // insertion or erasure invalidates all iterators, pointers and references.
    // erase(it) returns a valid iterator to the next element.
    //
    // The engine behind BTreeMap / BTreeSet; KeyOf extracts the key from T.
    template <class T, class KeyOf = Identity, class Cmp = Less>
    class BTree {
    protected:
        // Max elements per node (odd, 2t - 1). Smaller elements get wider nodes.
        static constexpr uint32 kCap = sizeof(T) <= 16 ? 31 : (sizeof(T) <= 64 ? 15 : 7);
        // Min elements per non-root node (t - 1).
        static constexpr uint32 kMin = kCap / 2;

        struct Inner;

        struct Node {
            Inner* parent = nullptr;
            uint16 pos = 0; // index in parent->kids
            uint16 count = 0;
            bool leaf;
            alignas(T) unsigned char storage[kCap * sizeof(T)];

            explicit Node(bool is_leaf) noexcept
                : leaf(is_leaf)
            {
            }

            T* slot(size_t i) noexcept {
                return yasl::launder(reinterpret_cast<T*>(storage) + i);
            }
        };

        struct Inner: Node {
            Node* kids[kCap + 1] = {};

            Inner() noexcept
                : Node(false)
            {
            }
        };

        struct Pos {
            Node* node = nullptr;
            uint32 idx = 0;
        };

        template <bool Const>
        class Iter {
            friend class BTree;
            template <bool>
            friend class Iter;

        public:
            using value_type = T;
            using difference_type = ptrdiff_t;
            using reference = Conditional<Const, const T&, T&>;
            using pointer = Conditional<Const, const T*, T*>;

            Iter() noexcept = default;

            template <bool C2>
                requires(Const && !C2)
            Iter(const Iter<C2>& other) noexcept
                : p_(other.p_)
                , t_(other.t_)
            {
            }

            reference operator*() const noexcept {
                return *p_.node->slot(p_.idx);
            }

            pointer operator->() const noexcept {
                return p_.node->slot(p_.idx);
            }

            Iter& operator++() noexcept {
                BTree::next(p_);
                return *this;
            }

            Iter operator++(int) noexcept {
                Iter copy = *this;
                ++*this;
                return copy;
            }

            Iter& operator--() noexcept {
                if (p_.node) {
                    BTree::prev(p_);
                } else {
                    p_ = t_->last_pos();
                }
                return *this;
            }

            Iter operator--(int) noexcept {
                Iter copy = *this;
                --*this;
                return copy;
            }

            friend bool operator==(const Iter& a, const Iter& b) noexcept {
                return a.p_.node == b.p_.node && a.p_.idx == b.p_.idx;
            }

        private:
            Iter(Pos p, const BTree* t) noexcept
                : p_(p)
                , t_(t)
            {
                if (!p_.node) {
                    p_.idx = 0;
                }
            }

            Pos p_;
            const BTree* t_ = nullptr;
        };

    public:
        using value_type = T;
        using key_type = RemoveCVRef<decltype(KeyOf{}(declval<const T&>()))>;
        using const_iterator = Iter<true>;
        using iterator = Conditional<SameAs<KeyOf, Identity>, Iter<true>, Iter<false>>;

        BTree() noexcept
            : BTree(mem::default_resource())
        {
        }

        explicit BTree(mem::Resource* res, Cmp cmp = Cmp()) noexcept
            : res_(res)
            , cmp_(yasl::move(cmp))
        {
        }

        BTree(std::initializer_list<T> il, mem::Resource* res = mem::default_resource(), Cmp cmp = Cmp())
            : BTree(res, yasl::move(cmp))
        {
            for (const T& v : il) {
                try_emplace_with(key_of(v), v);
            }
        }

        BTree(const BTree& other)
            : BTree(other, other.res_)
        {
        }

        BTree(const BTree& other, mem::Resource* res)
            : BTree(res, other.cmp_)
        {
            if (other.root_) {
                root_ = clone(other.root_, nullptr, 0);
                size_ = other.size_;
            }
        }

        BTree(BTree&& other) noexcept
            : root_(yasl::exchange(other.root_, nullptr))
            , size_(yasl::exchange(other.size_, size_t(0)))
            , res_(other.res_)
            , cmp_(yasl::move(other.cmp_))
        {
        }

        ~BTree() {
            clear();
        }

        BTree& operator=(const BTree& other) {
            if (this != &other) {
                BTree tmp(other, res_);
                swap(tmp);
            }
            return *this;
        }

        BTree& operator=(BTree&& other) noexcept {
            if (this != &other) {
                clear();
                root_ = yasl::exchange(other.root_, nullptr);
                size_ = yasl::exchange(other.size_, size_t(0));
                res_ = other.res_;
                cmp_ = yasl::move(other.cmp_);
            }
            return *this;
        }

        size_t size() const noexcept {
            return size_;
        }

        bool empty() const noexcept {
            return size_ == 0;
        }

        mem::Resource* resource() const noexcept {
            return res_;
        }

        iterator begin() noexcept {
            return {first_pos(), this};
        }

        iterator end() noexcept {
            return {Pos{}, this};
        }

        const_iterator begin() const noexcept {
            return {first_pos(), this};
        }

        const_iterator end() const noexcept {
            return {Pos{}, this};
        }

        const T& front() const noexcept {
            YASL_ASSERT(root_);
            Pos p = first_pos();
            return *p.node->slot(p.idx);
        }

        const T& back() const noexcept {
            YASL_ASSERT(root_);
            Pos p = last_pos();
            return *p.node->slot(p.idx);
        }

        template <class Q>
        iterator find(const Q& key) noexcept {
            return {find_pos(key), this};
        }

        template <class Q>
        const_iterator find(const Q& key) const noexcept {
            return {find_pos(key), this};
        }

        template <class Q>
        bool contains(const Q& key) const noexcept {
            return find_pos(key).node != nullptr;
        }

        template <class Q>
        iterator lower_bound(const Q& key) noexcept {
            return {bound_pos(key, false), this};
        }

        template <class Q>
        const_iterator lower_bound(const Q& key) const noexcept {
            return {bound_pos(key, false), this};
        }

        template <class Q>
        iterator upper_bound(const Q& key) noexcept {
            return {bound_pos(key, true), this};
        }

        template <class Q>
        const_iterator upper_bound(const Q& key) const noexcept {
            return {bound_pos(key, true), this};
        }

        // If `key` is absent, constructs T(args...) and inserts it.
        template <class Q, class... Args>
        Pair<iterator, bool> try_emplace_with(const Q& key, Args&&... args) {
            if (Pos p = find_pos(key); p.node) {
                return {iterator(p, this), false};
            }
            // Build the element first: if that throws, the tree is untouched.
            T tmp(yasl::forward<Args>(args)...);
            Pos p = insert_value(yasl::move(tmp));
            ++size_;
            return {iterator(p, this), true};
        }

        // Removes the element, returns the iterator to the following one.
        iterator erase(const_iterator it) {
            alignas(T) unsigned char buf[sizeof(T)];
            T* removed = reinterpret_cast<T*>(buf);
            remove_at(it.p_, removed);
            --size_;
            // Elements moved around; find the successor by key.
            Pos next = bound_pos(key_of(*removed), true);
            yasl::destroy_at(removed);
            return {next, this};
        }

        template <class Q>
            requires(!ConvertibleTo<const Q&, const_iterator>)
        size_t erase(const Q& key) {
            Pos p = find_pos(key);
            if (!p.node) {
                return 0;
            }
            remove_at(p, nullptr);
            --size_;
            return 1;
        }

        void clear() noexcept {
            if (root_) {
                destroy_subtree(root_);
                root_ = nullptr;
            }
            size_ = 0;
        }

        void swap(BTree& other) noexcept {
            yasl::swap(root_, other.root_);
            yasl::swap(size_, other.size_);
            yasl::swap(res_, other.res_);
            yasl::swap(cmp_, other.cmp_);
        }

        friend bool operator==(const BTree& a, const BTree& b)
            requires EqualityComparable<T>
        {
            if (a.size() != b.size()) {
                return false;
            }
            for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
                if (!(*i == *j)) {
                    return false;
                }
            }
            return true;
        }

        // Checks every structural invariant. Meant for tests and debugging.
        bool validate() const noexcept {
            if (!root_) {
                return size_ == 0;
            }
            if (root_->parent) {
                return false;
            }
            size_t count = 0;
            int leaf_depth = -1;
            return validate_node(root_, nullptr, nullptr, 0, leaf_depth, count) && count == size_;
        }

    protected:
        static const key_type& key_of(const T& v) noexcept {
            return KeyOf{}(v);
        }

        static const key_type& key_at(Node* n, uint32 i) noexcept {
            return KeyOf{}(*n->slot(i));
        }

        static Inner* inner(Node* n) noexcept {
            return static_cast<Inner*>(n);
        }

        static void set_child(Inner* p, uint32 i, Node* c) noexcept {
            p->kids[i] = c;
            c->parent = p;
            c->pos = uint16(i);
        }

        // Moves *src into the raw slot dst and ends src's lifetime.
        static void move_slot(T* dst, T* src) noexcept {
            if constexpr (TriviallyCopyable<T>) {
                yasl::mem_copy(static_cast<void*>(dst), static_cast<const void*>(src), sizeof(T));
            } else {
                yasl::construct_at(dst, yasl::move(*src));
                yasl::destroy_at(src);
            }
        }

        // Opens a hole at `at` by shifting [at, count) one slot right.
        static void shift_right(Node* n, uint32 at) noexcept {
            for (uint32 j = n->count; j > at; --j) {
                move_slot(n->slot(j), n->slot(j - 1));
            }
        }

        // Closes the hole at `at` by shifting (at, count) one slot left.
        static void shift_left(Node* n, uint32 at) noexcept {
            for (uint32 j = at; j + 1 < n->count; ++j) {
                move_slot(n->slot(j), n->slot(j + 1));
            }
        }

        static void next(Pos& p) noexcept {
            Node* n = p.node;
            if (!n->leaf) {
                n = inner(n)->kids[p.idx + 1];
                while (!n->leaf) {
                    n = inner(n)->kids[0];
                }
                p = {n, 0};
                return;
            }
            uint32 i = p.idx + 1;
            while (i == n->count) {
                if (!n->parent) {
                    p = {};
                    return;
                }
                i = n->pos;
                n = n->parent;
            }
            p = {n, i};
        }

        static void prev(Pos& p) noexcept {
            Node* n = p.node;
            if (!n->leaf) {
                n = inner(n)->kids[p.idx];
                while (!n->leaf) {
                    n = inner(n)->kids[n->count];
                }
                p = {n, uint32(n->count - 1)};
                return;
            }
            uint32 i = p.idx;
            while (i == 0) {
                if (!n->parent) {
                    p = {};
                    return;
                }
                i = n->pos;
                n = n->parent;
            }
            p = {n, i - 1};
        }

        Pos first_pos() const noexcept {
            Node* n = root_;
            if (!n) {
                return {};
            }
            while (!n->leaf) {
                n = inner(n)->kids[0];
            }
            return {n, 0};
        }

        Pos last_pos() const noexcept {
            Node* n = root_;
            if (!n) {
                return {};
            }
            while (!n->leaf) {
                n = inner(n)->kids[n->count];
            }
            return {n, uint32(n->count - 1)};
        }

        // First index in n with !(key_at(i) < key).
        template <class Q>
        uint32 lower_index(Node* n, const Q& key) const noexcept {
            uint32 lo = 0;
            uint32 hi = n->count;
            while (lo < hi) {
                uint32 mid = (lo + hi) / 2;
                if (cmp_(key_at(n, mid), key)) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            return lo;
        }

        template <class Q>
        Pos find_pos(const Q& key) const noexcept {
            Node* n = root_;
            while (n) {
                uint32 i = lower_index(n, key);
                if (i < n->count && !cmp_(key, key_at(n, i))) {
                    return {n, i};
                }
                if (n->leaf) {
                    return {};
                }
                n = inner(n)->kids[i];
            }
            return {};
        }

        // First element with key >= `key` (or > `key` if `upper`).
        template <class Q>
        Pos bound_pos(const Q& key, bool upper) const noexcept {
            Pos res;
            Node* n = root_;
            while (n) {
                uint32 lo = 0;
                uint32 hi = n->count;
                while (lo < hi) {
                    uint32 mid = (lo + hi) / 2;
                    bool go_right = upper ? !cmp_(key, key_at(n, mid)) : cmp_(key_at(n, mid), key);
                    if (go_right) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                if (lo < n->count) {
                    res = {n, lo};
                }
                if (n->leaf) {
                    break;
                }
                n = inner(n)->kids[lo];
            }
            return res;
        }

        Node* new_leaf() {
            return res_->template create<Node>(true);
        }

        Inner* new_inner() {
            return res_->template create<Inner>();
        }

        void free_node(Node* n) noexcept {
            if (n->leaf) {
                res_->destroy(n);
            } else {
                res_->destroy(inner(n));
            }
        }

        // Splits the full child p->kids[i]; its median moves up into p at index i.
        void split_child(Inner* p, uint32 i) {
            Node* y = p->kids[i];
            Node* z = y->leaf ? new_leaf() : static_cast<Node*>(new_inner());
            constexpr uint32 t = kMin + 1;
            for (uint32 j = 0; j < kMin; ++j) {
                move_slot(z->slot(j), y->slot(t + j));
            }
            if (!y->leaf) {
                for (uint32 j = 0; j <= kMin; ++j) {
                    set_child(inner(z), j, inner(y)->kids[t + j]);
                }
            }
            z->count = uint16(kMin);

            shift_right(p, i);
            for (uint32 j = p->count + 1; j > i + 1; --j) {
                set_child(p, j, p->kids[j - 1]);
            }
            move_slot(p->slot(i), y->slot(kMin));
            y->count = uint16(kMin);
            set_child(p, i + 1, z);
            ++p->count;
        }

        // Inserts a value whose key is known to be absent (top-down, splitting full nodes).
        Pos insert_value(T&& v) {
            if (!root_) {
                root_ = new_leaf();
            }
            if (root_->count == kCap) {
                Inner* r = new_inner();
                Node* old = root_;
                set_child(r, 0, old);
                try {
                    split_child(r, 0);
                } catch (...) {
                    old->parent = nullptr;
                    res_->destroy(r);
                    throw;
                }
                root_ = r;
            }
            const key_type& key = key_of(v);
            Node* n = root_;
            for (;;) {
                uint32 i = lower_index(n, key);
                if (n->leaf) {
                    shift_right(n, i);
                    yasl::construct_at(n->slot(i), yasl::move(v));
                    ++n->count;
                    return {n, i};
                }
                Inner* in = inner(n);
                if (in->kids[i]->count == kCap) {
                    split_child(in, i);
                    if (cmp_(key_at(in, i), key)) {
                        ++i;
                    }
                }
                n = in->kids[i];
            }
        }

        // Removes the element at p (moving it into *out if out != nullptr).
        void remove_at(Pos p, T* out) noexcept {
            Node* n = p.node;
            uint32 i = p.idx;
            if (out) {
                yasl::construct_at(out, yasl::move(*n->slot(i)));
            }
            yasl::destroy_at(n->slot(i));
            if (!n->leaf) {
                // Replace with the predecessor, which lives in a leaf.
                Node* leaf = inner(n)->kids[i];
                while (!leaf->leaf) {
                    leaf = inner(leaf)->kids[leaf->count];
                }
                move_slot(n->slot(i), leaf->slot(leaf->count - 1));
                --leaf->count;
                rebalance(leaf);
            } else {
                shift_left(n, i);
                --n->count;
                rebalance(n);
            }
        }

        void rebalance(Node* n) noexcept {
            while (n != root_ && n->count < kMin) {
                Inner* p = n->parent;
                uint32 idx = n->pos;
                Node* left = idx > 0 ? p->kids[idx - 1] : nullptr;
                Node* right = idx < p->count ? p->kids[idx + 1] : nullptr;
                if (left && left->count > kMin) {
                    rotate_right(p, idx - 1);
                    return;
                }
                if (right && right->count > kMin) {
                    rotate_left(p, idx);
                    return;
                }
                merge(p, left ? idx - 1 : idx);
                n = p;
            }
            if (root_->count == 0) {
                Node* old = root_;
                if (old->leaf) {
                    root_ = nullptr;
                } else {
                    root_ = inner(old)->kids[0];
                    root_->parent = nullptr;
                    root_->pos = 0;
                }
                free_node(old);
            }
        }

        // Moves one element from kids[s] through separator s into kids[s + 1].
        void rotate_right(Inner* p, uint32 s) noexcept {
            Node* left = p->kids[s];
            Node* right = p->kids[s + 1];
            shift_right(right, 0);
            move_slot(right->slot(0), p->slot(s));
            move_slot(p->slot(s), left->slot(left->count - 1));
            if (!right->leaf) {
                for (uint32 j = right->count + 1; j > 0; --j) {
                    set_child(inner(right), j, inner(right)->kids[j - 1]);
                }
                set_child(inner(right), 0, inner(left)->kids[left->count]);
            }
            --left->count;
            ++right->count;
        }

        // Moves one element from kids[s + 1] through separator s into kids[s].
        void rotate_left(Inner* p, uint32 s) noexcept {
            Node* left = p->kids[s];
            Node* right = p->kids[s + 1];
            move_slot(left->slot(left->count), p->slot(s));
            move_slot(p->slot(s), right->slot(0));
            if (!left->leaf) {
                set_child(inner(left), left->count + 1u, inner(right)->kids[0]);
            }
            shift_left(right, 0);
            if (!right->leaf) {
                for (uint32 j = 0; j < right->count; ++j) {
                    set_child(inner(right), j, inner(right)->kids[j + 1]);
                }
            }
            ++left->count;
            --right->count;
        }

        // Merges kids[s], separator s and kids[s + 1] into kids[s].
        void merge(Inner* p, uint32 s) noexcept {
            Node* left = p->kids[s];
            Node* right = p->kids[s + 1];
            uint32 base = left->count;
            move_slot(left->slot(base), p->slot(s));
            for (uint32 j = 0; j < right->count; ++j) {
                move_slot(left->slot(base + 1 + j), right->slot(j));
            }
            if (!left->leaf) {
                for (uint32 j = 0; j <= right->count; ++j) {
                    set_child(inner(left), base + 1 + j, inner(right)->kids[j]);
                }
            }
            left->count = uint16(base + 1 + right->count);

            shift_left(p, s);
            for (uint32 j = s + 1; j < p->count; ++j) {
                set_child(p, j, p->kids[j + 1]);
            }
            --p->count;
            right->count = 0;
            free_node(right);
        }

        Node* clone(Node* src, Inner* parent, uint32 pos) {
            Node* dst = src->leaf ? new_leaf() : static_cast<Node*>(new_inner());
            dst->parent = parent;
            dst->pos = uint16(pos);
            try {
                for (uint32 j = 0; j < src->count; ++j) {
                    yasl::construct_at(dst->slot(j), *src->slot(j));
                    dst->count = uint16(j + 1);
                }
                if (!src->leaf) {
                    for (uint32 j = 0; j <= src->count; ++j) {
                        inner(dst)->kids[j] = clone(inner(src)->kids[j], inner(dst), j);
                    }
                }
            } catch (...) {
                destroy_subtree(dst);
                throw;
            }
            return dst;
        }

        void destroy_subtree(Node* n) noexcept {
            if (!n->leaf) {
                for (uint32 j = 0; j <= n->count; ++j) {
                    if (Node* kid = inner(n)->kids[j]) {
                        destroy_subtree(kid);
                    }
                }
            }
            yasl::destroy_n(n->slot(0), n->count);
            free_node(n);
        }

        bool validate_node(Node* n, const T* lo, const T* hi, int depth, int& leaf_depth, size_t& count) const noexcept {
            if (n != root_ && (n->count < kMin || n->count > kCap)) {
                return false;
            }
            for (uint32 j = 0; j < n->count; ++j) {
                const key_type& k = key_at(n, j);
                if (j > 0 && !cmp_(key_at(n, j - 1), k)) {
                    return false;
                }
                if ((lo && !cmp_(key_of(*lo), k)) || (hi && !cmp_(k, key_of(*hi)))) {
                    return false;
                }
            }
            count += n->count;
            if (n->leaf) {
                if (leaf_depth < 0) {
                    leaf_depth = depth;
                }
                return leaf_depth == depth;
            }
            for (uint32 j = 0; j <= n->count; ++j) {
                Node* kid = inner(n)->kids[j];
                if (!kid || kid->parent != n || kid->pos != j) {
                    return false;
                }
                const T* klo = j > 0 ? n->slot(j - 1) : lo;
                const T* khi = j < n->count ? n->slot(j) : hi;
                if (!validate_node(kid, klo, khi, depth + 1, leaf_depth, count)) {
                    return false;
                }
            }
            return true;
        }

        Node* root_ = nullptr;
        size_t size_ = 0;
        mem::Resource* res_;
        [[no_unique_address]] Cmp cmp_;
    };
}
