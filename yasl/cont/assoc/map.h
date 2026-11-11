#pragma once

#include "map_ops.h"
#include "btree.h"
#include "treap.h"

namespace yasl {
// Ordered map on a treap. Node-based: iterators survive other
// insertions/erasures. Extras: kth, rank, index_of, split, join.
template <class K, class V, class Cmp = Less>
class TreapMap : public Treap<MapEntry<K, V>, EntryKey, Cmp>,
                 public MapOps<TreapMap<K, V, Cmp>, V> {
 public:
  using key_type = K;
  using mapped_type = V;
  using Treap<MapEntry<K, V>, EntryKey, Cmp>::Treap;
};

template <class K, class Cmp = Less>
class TreapSet : public Treap<K, Identity, Cmp>,
                 public SetOps<TreapSet<K, Cmp>, K> {
 public:
  using Treap<K, Identity, Cmp>::Treap;
};

// Ordered map on a B-tree. Faster and more compact, but every insertion or
// erasure invalidates iterators and references.
template <class K, class V, class Cmp = Less>
class BTreeMap : public BTree<MapEntry<K, V>, EntryKey, Cmp>,
                 public MapOps<BTreeMap<K, V, Cmp>, V> {
 public:
  using key_type = K;
  using mapped_type = V;
  using BTree<MapEntry<K, V>, EntryKey, Cmp>::BTree;
};

template <class K, class Cmp = Less>
class BTreeSet : public BTree<K, Identity, Cmp>,
                 public SetOps<BTreeSet<K, Cmp>, K> {
 public:
  using BTree<K, Identity, Cmp>::BTree;
};

// Defaults: the B-tree, unless you need stable iterators or order statistics.
template <class K, class V, class Cmp = Less>
using Map = BTreeMap<K, V, Cmp>;

template <class K, class Cmp = Less>
using Set = BTreeSet<K, Cmp>;

}  // namespace yasl
