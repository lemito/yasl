#pragma once

#include <yasl/mem/resource.h>
#include <yasl/sys/check.h>
#include <yasl/sys/utility.h>

#include <initializer_list> // language support for braced lists

namespace yasl {
    // Ordered container on a randomized treap (BST by key + max-heap by random priority).
    //
    // Node-based: iterators and references stay valid until their element is erased.
    // Besides the usual lookup it supports, all in O(log n):
    //   * kth(i)  - i-th element in order;
    //   * rank(k) - number of elements with key < k;
    //   * split(k, rest) - move every element with key >= k into `rest`;
    //   * join(other)    - append a treap whose keys are all greater.
    //
    // The engine behind TreapMap / TreapSet; KeyOf extracts the key from T.
    template <class T, class KeyOf = Identity, class Cmp = Less>
    class Treap {
    protected:
        struct Node {
            T value;
            Node* left = nullptr;
            Node* right = nullptr;
            Node* parent = nullptr;
            uint64 prio;
            size_t size = 1;

            template <class... Args>
            explicit Node(uint64 p, Args&&... args)
                : value(yasl::forward<Args>(args)...)
                , prio(p)
            {
            }
        };

        template <bool Const>
        class Iter {
            friend class Treap;
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
                : n_(other.n_)
                , t_(other.t_)
            {
            }

            reference operator*() const noexcept {
                return n_->value;
            }

            pointer operator->() const noexcept {
                return &n_->value;
            }

            Iter& operator++() noexcept {
                n_ = Treap::next(n_);
                return *this;
            }

            Iter operator++(int) noexcept {
                Iter copy = *this;
                ++*this;
                return copy;
            }

            Iter& operator--() noexcept {
                n_ = n_ ? Treap::prev(n_) : Treap::max_node(t_->root_);
                return *this;
            }

            Iter operator--(int) noexcept {
                Iter copy = *this;
                --*this;
                return copy;
            }

            friend bool operator==(const Iter& a, const Iter& b) noexcept {
                return a.n_ == b.n_;
            }

        private:
            Iter(Node* n, const Treap* t) noexcept
                : n_(n)
                , t_(t)
            {
            }

            Node* n_ = nullptr;
            const Treap* t_ = nullptr;
        };

    public:
        using value_type = T;
        using key_type = RemoveCVRef<decltype(KeyOf{}(declval<const T&>()))>;
        using const_iterator = Iter<true>;
        // Set elements are keys, so they are never mutable through an iterator.
        using iterator = Conditional<SameAs<KeyOf, Identity>, Iter<true>, Iter<false>>;

        Treap() noexcept
            : Treap(mem::default_resource())
        {
        }

        explicit Treap(mem::Resource* res, Cmp cmp = Cmp()) noexcept
            : res_(res)
            , cmp_(yasl::move(cmp))
            , rng_(0x2545F4914F6CDD1Dull ^ reinterpret_cast<uintptr_t>(this))
        {
        }

        Treap(std::initializer_list<T> il, mem::Resource* res = mem::default_resource(), Cmp cmp = Cmp())
            : Treap(res, yasl::move(cmp))
        {
            for (const T& v : il) {
                try_emplace_with(key_of(v), v);
            }
        }

        Treap(const Treap& other)
            : Treap(other, other.res_)
        {
        }

        Treap(const Treap& other, mem::Resource* res)
            : Treap(res, other.cmp_)
        {
            root_ = clone(other.root_, nullptr);
        }

        Treap(Treap&& other) noexcept
            : root_(yasl::exchange(other.root_, nullptr))
            , res_(other.res_)
            , cmp_(yasl::move(other.cmp_))
            , rng_(other.rng_)
        {
        }

        ~Treap() {
            clear();
        }

        Treap& operator=(const Treap& other) {
            if (this != &other) {
                Treap tmp(other, res_);
                swap(tmp);
            }
            return *this;
        }

        Treap& operator=(Treap&& other) noexcept {
            if (this != &other) {
                clear();
                root_ = yasl::exchange(other.root_, nullptr);
                res_ = other.res_;
                cmp_ = yasl::move(other.cmp_);
            }
            return *this;
        }

        // Capacity

        size_t size() const noexcept {
            return sz(root_);
        }

        bool empty() const noexcept {
            return root_ == nullptr;
        }

        mem::Resource* resource() const noexcept {
            return res_;
        }

        // Iteration

        iterator begin() noexcept {
            return {min_node(root_), this};
        }

        iterator end() noexcept {
            return {nullptr, this};
        }

        const_iterator begin() const noexcept {
            return {min_node(root_), this};
        }

        const_iterator end() const noexcept {
            return {nullptr, this};
        }

        const T& front() const noexcept {
            YASL_ASSERT(root_);
            return min_node(root_)->value;
        }

        const T& back() const noexcept {
            YASL_ASSERT(root_);
            return max_node(root_)->value;
        }

        // Lookup

        template <class Q>
        iterator find(const Q& key) noexcept {
            return {find_node(key), this};
        }

        template <class Q>
        const_iterator find(const Q& key) const noexcept {
            return {find_node(key), this};
        }

        template <class Q>
        bool contains(const Q& key) const noexcept {
            return find_node(key) != nullptr;
        }

        // First element with key >= `key`.
        template <class Q>
        iterator lower_bound(const Q& key) noexcept {
            return {lower_node(key), this};
        }

        template <class Q>
        const_iterator lower_bound(const Q& key) const noexcept {
            return {lower_node(key), this};
        }

        // First element with key > `key`.
        template <class Q>
        iterator upper_bound(const Q& key) noexcept {
            return {upper_node(key), this};
        }

        template <class Q>
        const_iterator upper_bound(const Q& key) const noexcept {
            return {upper_node(key), this};
        }

        // i-th element in sorted order (0-based), end() if i >= size().
        iterator kth(size_t i) noexcept {
            return {kth_node(i), this};
        }

        const_iterator kth(size_t i) const noexcept {
            return {kth_node(i), this};
        }

        // Number of elements with key < `key`.
        template <class Q>
        size_t rank(const Q& key) const noexcept {
            size_t r = 0;
            for (Node* n = root_; n;) {
                if (cmp_(key_of(n), key)) {
                    r += sz(n->left) + 1;
                    n = n->right;
                } else {
                    n = n->left;
                }
            }
            return r;
        }

        // Position of an element in sorted order.
        size_t index_of(const_iterator it) const noexcept {
            Node* n = it.n_;
            if (!n) {
                return size();
            }
            size_t r = sz(n->left);
            for (; n->parent; n = n->parent) {
                if (n == n->parent->right) {
                    r += sz(n->parent->left) + 1;
                }
            }
            return r;
        }

        // Modifiers

        // If `key` is absent, constructs T(args...) and inserts it. `key` is used only for
        // lookup; T(args...) must produce an element with an equivalent key.
        template <class Q, class... Args>
        Pair<iterator, bool> try_emplace_with(const Q& key, Args&&... args) {
            if (Node* found = find_node(key)) {
                return {iterator(found, this), false};
            }
            Node* n = res_->template create<Node>(next_prio(), yasl::forward<Args>(args)...);
            insert_node(n);
            return {iterator(n, this), true};
        }

        // Removes the element, returns the iterator to the following one.
        iterator erase(const_iterator it) noexcept {
            Node* n = it.n_;
            YASL_ASSERT(n);
            Node* following = next(n);
            Node* p = n->parent;
            Node* m = merge(n->left, n->right);
            if (m) {
                m->parent = p;
            }
            if (!p) {
                root_ = m;
            } else if (p->left == n) {
                p->left = m;
            } else {
                p->right = m;
            }
            for (Node* q = p; q; q = q->parent) {
                --q->size;
            }
            res_->destroy(n);
            return {following, this};
        }

        template <class Q>
            requires(!ConvertibleTo<const Q&, const_iterator>)
        size_t erase(const Q& key) noexcept {
            Node* n = find_node(key);
            if (!n) {
                return 0;
            }
            erase(const_iterator(n, this));
            return 1;
        }

        void clear() noexcept {
            destroy_subtree(root_);
            root_ = nullptr;
        }

        // Moves every element with key >= `key` into `rest` (its old contents are dropped).
        // `rest` takes over this treap's resource and comparator.
        template <class Q>
        void split(const Q& key, Treap& rest) {
            YASL_ASSERT(&rest != this);
            rest.clear();
            rest.res_ = res_;
            rest.cmp_ = cmp_;
            Node* a;
            Node* b;
            split_nodes(root_, key, a, b);
            root_ = detach(a);
            rest.root_ = detach(b);
        }

        // Appends every element of `other`, whose keys must all be greater than ours.
        // O(log n) when both use the same resource; `other` is left empty.
        void join(Treap& other) {
            YASL_ASSERT(&other != this);
            if (!other.root_) {
                return;
            }
            YASL_ASSERT(!root_ || cmp_(key_of(max_node(root_)), key_of(min_node(other.root_))));
            if (res_->is_equal(*other.res_)) {
                root_ = detach(merge(root_, yasl::exchange(other.root_, nullptr)));
                return;
            }
            for (Node* n = min_node(other.root_); n; n = next(n)) {
                insert_node(res_->template create<Node>(next_prio(), yasl::move(n->value)));
            }
            other.clear();
        }

        void swap(Treap& other) noexcept {
            yasl::swap(root_, other.root_);
            yasl::swap(res_, other.res_);
            yasl::swap(cmp_, other.cmp_);
        }

        friend bool operator==(const Treap& a, const Treap& b)
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
            if (root_ && root_->parent) {
                return false;
            }
            return validate_node(root_);
        }

    protected:
        static size_t sz(const Node* n) noexcept {
            return n ? n->size : 0;
        }

        static const key_type& key_of(const T& v) noexcept {
            return KeyOf{}(v);
        }

        static const key_type& key_of(const Node* n) noexcept {
            return KeyOf{}(n->value);
        }

        static Node* min_node(Node* n) noexcept {
            if (n) {
                while (n->left) {
                    n = n->left;
                }
            }
            return n;
        }

        static Node* max_node(Node* n) noexcept {
            if (n) {
                while (n->right) {
                    n = n->right;
                }
            }
            return n;
        }

        static Node* next(Node* n) noexcept {
            if (n->right) {
                return min_node(n->right);
            }
            while (n->parent && n == n->parent->right) {
                n = n->parent;
            }
            return n->parent;
        }

        static Node* prev(Node* n) noexcept {
            if (n->left) {
                return max_node(n->left);
            }
            while (n->parent && n == n->parent->left) {
                n = n->parent;
            }
            return n->parent;
        }

        // Recomputes the size and re-links the children's parent pointers.
        static void pull(Node* n) noexcept {
            n->size = 1 + sz(n->left) + sz(n->right);
            if (n->left) {
                n->left->parent = n;
            }
            if (n->right) {
                n->right->parent = n;
            }
        }

        static Node* detach(Node* n) noexcept {
            if (n) {
                n->parent = nullptr;
            }
            return n;
        }

        // Every key in `a` is less than every key in `b`.
        static Node* merge(Node* a, Node* b) noexcept {
            if (!a) {
                return b;
            }
            if (!b) {
                return a;
            }
            if (a->prio > b->prio) {
                a->right = merge(a->right, b);
                pull(a);
                return a;
            }
            b->left = merge(a, b->left);
            pull(b);
            return b;
        }

        // Splits `t` into keys < `key` (a) and keys >= `key` (b).
        template <class Q>
        void split_nodes(Node* t, const Q& key, Node*& a, Node*& b) const {
            if (!t) {
                a = b = nullptr;
                return;
            }
            if (cmp_(key_of(t), key)) {
                split_nodes(t->right, key, t->right, b);
                a = t;
            } else {
                split_nodes(t->left, key, a, t->left);
                b = t;
            }
            pull(t);
        }

        // Inserts a node whose key is known to be absent.
        void insert_node(Node* n) {
            const key_type& key = key_of(n);
            Node* parent = nullptr;
            Node** link = &root_;
            while (*link && (*link)->prio >= n->prio) {
                parent = *link;
                ++parent->size;
                link = cmp_(key, key_of(parent)) ? &parent->left : &parent->right;
            }
            split_nodes(*link, key, n->left, n->right);
            pull(n);
            n->parent = parent;
            *link = n;
        }

        template <class Q>
        Node* find_node(const Q& key) const noexcept {
            Node* n = root_;
            while (n) {
                if (cmp_(key, key_of(n))) {
                    n = n->left;
                } else if (cmp_(key_of(n), key)) {
                    n = n->right;
                } else {
                    return n;
                }
            }
            return nullptr;
        }

        template <class Q>
        Node* lower_node(const Q& key) const noexcept {
            Node* res = nullptr;
            for (Node* n = root_; n;) {
                if (!cmp_(key_of(n), key)) {
                    res = n;
                    n = n->left;
                } else {
                    n = n->right;
                }
            }
            return res;
        }

        template <class Q>
        Node* upper_node(const Q& key) const noexcept {
            Node* res = nullptr;
            for (Node* n = root_; n;) {
                if (cmp_(key, key_of(n))) {
                    res = n;
                    n = n->left;
                } else {
                    n = n->right;
                }
            }
            return res;
        }

        Node* kth_node(size_t i) const noexcept {
            Node* n = root_;
            while (n) {
                size_t ls = sz(n->left);
                if (i < ls) {
                    n = n->left;
                } else if (i == ls) {
                    return n;
                } else {
                    i -= ls + 1;
                    n = n->right;
                }
            }
            return nullptr;
        }

        Node* clone(const Node* n, Node* parent) {
            if (!n) {
                return nullptr;
            }
            Node* c = res_->template create<Node>(n->prio, n->value);
            c->size = n->size;
            c->parent = parent;
            try {
                c->left = clone(n->left, c);
                c->right = clone(n->right, c);
            } catch (...) {
                destroy_subtree(c);
                throw;
            }
            return c;
        }

        void destroy_subtree(Node* n) noexcept {
            while (n) {
                destroy_subtree(n->left);
                Node* right = n->right;
                res_->destroy(n);
                n = right;
            }
        }

        bool validate_node(const Node* n) const noexcept {
            if (!n) {
                return true;
            }
            for (const Node* c : {n->left, n->right}) {
                if (c && (c->parent != n || c->prio > n->prio)) {
                    return false;
                }
            }
            if (n->left && !cmp_(key_of(max_node(n->left)), key_of(n))) {
                return false;
            }
            if (n->right && !cmp_(key_of(n), key_of(min_node(n->right)))) {
                return false;
            }
            return n->size == 1 + sz(n->left) + sz(n->right) && validate_node(n->left) && validate_node(n->right);
        }

        uint64 next_prio() noexcept {
            return mix(rng_ += 0x9E3779B97F4A7C15ull);
        }

        static uint64 mix(uint64 z) noexcept {
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        Node* root_ = nullptr;
        mem::Resource* res_;
        [[no_unique_address]] Cmp cmp_;
        uint64 rng_;
    };
}
