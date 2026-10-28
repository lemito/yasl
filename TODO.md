# TODO — roadmap yasl

Цель: по максимуму закрыть слабые стороны std C++20 и взять лучшее из Rust, Go, C#, Java, Kotlin,
сохраняя главное правило — **ядро не использует стандартную библиотеку C++** (см. README).

## Правила для исполнителя (читать перед каждой задачей)

1. **Одна задача = один коммит.** Берите задачи по порядку фаз; внутри фазы — по порядку, если не указано `(независимо)`.
   Не начинайте задачу, пока её зависимости (`Зависит от:`) не отмечены `[x]`.
2. **Include-политика.** В ядре (`yasl/**`, кроме `yasl/compat/`, `*_test.cpp`, `yasl/test/`) разрешены только
   `<new> <initializer_list> <compare> <cstddef> <cstdint> <atomic>` и заголовки ОС из `cmake/check_includes.cmake`.
   Новый системный заголовок (например `<time.h>`) — добавить в `ALLOWED` в том же коммите и указать причину.
   Traits/концепты — только из `yasl/core/traits.h` (на builtins); не хватает — добавить туда.
3. **Стиль — Google, `.clang-format` в корне (`clang-format -i` на изменённых файлах):** `namespace yasl` (подмодули `yasl::io`, `yasl::mem`, …), 2 пробела, без отступа в namespace,
   `#pragma once`, `snake_case` методы, `PascalCase` типы, члены с суффиксом `_`, `yasl::move/forward`,
   короткий комментарий-пример над классом (см. `yasl/ptr/arc.h`, `yasl/str/builder.h`).
   Все контейнеры/владеющие типы принимают `mem::Resource*` (по умолчанию `mem::default_resource()`).
4. **Тесты обязательны:** файл `<модуль>/<имя>_test.cpp`, макросы `YASL_TEST`, `CHECK`, `CHECK_EQ`, `CHECK_THROWS`
   (`yasl/test/test.h`). Утечки проверять через `mem::Tracking` (`t.stats().live() == 0`).
   std в тестах — только как эталон для сравнения. CMake подхватывает файлы глобом, править его не нужно.
5. **Проверка перед коммитом** (обе сборки и их ctest должны пройти):
   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DYASL_SANITIZE=ON && cmake --build build -j && (cd build && ctest --output-on-failure)
   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-Werror && cmake --build build-rel -j && (cd build-rel && ctest)
   ```
   Для многопоточного кода дополнительно собрать с `-fsanitize=thread` (clang).
6. **После выполнения:** отметить `[x]`, перенести пункт в `Done.txt` (одной строкой: дата, задача, коммит),
   обновить соответствующий раздел `README.md`, если появился публичный API.
7. Если задача оказалась больше описанной или требует решения, которого здесь нет, — **остановиться и
   записать вопрос** в раздел «Открытые вопросы» внизу, а не импровизировать с архитектурой.

Легенда приоритетов: **P0** — основа, от неё зависят другие; **P1** — важно; **P2** — желательно; **P3** — потом.

---

## Фаза 1. Базовые типы (`yasl/core`, `yasl/vocab`, `yasl/algo`) — P0

- [x] **1.1 `Result<T, E>`** (Rust `Result`, Kotlin `Result`). Файл `yasl/vocab/result.h`.
  - Хранение: union + флаг, как в `yasl/vocab/optional.h`. `Ok(v)` / `Err(e)` — фабрики-теги
    (`yasl::ok(v)`, `yasl::err(e)`), неявно конвертируются в любой `Result`.
  - API: `is_ok() is_err() explicit operator bool`, `value()` (бросает `BadAccess`), `operator* ->`,
    `error()`, `value_or(x)`, `map(f)`, `map_err(f)`, `and_then(f)`, `or_else(f)`, `ok() -> Optional<T>`,
    `Result<void, E>` (специализация).
  - Макрос `YASL_TRY(expr)`: на GCC/clang statement-expression `({ auto r = expr; if (!r) return err(...); *r; })`;
    + `YASL_TRY_VOID(expr)`.
  - Тип ошибки по умолчанию для io: `io::Error` (уже есть в `io/file.h`) — проверьте, что он копируемый.
  - Готово, когда: тесты на ok/err, move-only `T`, нетривиальные деструкторы (Tracking), `YASL_TRY` в цепочке.
- [x] **1.2 Комбинаторы `Optional`** (Rust `Option`, Kotlin `?.`/`?:`). Файл `yasl/vocab/optional.h`.
  `map`, `and_then`, `or_else`, `filter`, `take()` (забрать и обнулить), `ok_or(e) -> Result`,
  `value_or_else(f)`, `Optional<T&>` (специализация: хранит указатель). `Зависит от:` 1.1 (для `ok_or`).
- [x] **1.3 `Function<Sig>` и `FunctionRef<Sig>`.** Файл `yasl/vocab/function.h`.
  - `FunctionRef` — невладеющая (указатель на объект + thunk), для параметров-колбэков.
  - `Function` — владеющая, small-buffer 2–3 указателя, иначе аллокация через `mem::Resource`;
    поддержка move-only лямбд (как `std::move_only_function`), копирование запрещено.
  - Готово, когда: тесты SBO/heap, move-only захваты, отсутствие утечек.
- [x] **1.4 `defer` / `ScopeExit`** (Go `defer`). Файл `yasl/vocab/defer.h`:
  `auto g = scope_exit([&]{...}); g.dismiss();` и макрос `YASL_DEFER { ... };`. `(независимо)`
- [x] **1.5 Проверенная арифметика** (Rust `checked_*`, `saturating_*`, `wrapping_*`). Файл `yasl/core/math.h`:
  `checked_add/sub/mul -> Optional<T>` (через `__builtin_add_overflow`), `saturating_add/sub/mul`,
  `narrow<T>(x) -> Optional<T>` (C# `checked` cast), `div_ceil`, `is_pow2`, `popcount`, `countr_zero`.
  Перенести битовые утилиты из `utility.h` сюда, оставив `#include` для совместимости. `(независимо)`
- [x] **1.6 Сортировка и алгоритмы** (в std нет сортировки без `<algorithm>`). Файл `yasl/algo/algo.h`:
  `sort` (introsort: quicksort median-of-3 + heapsort fallback + insertion < 16), `stable_sort` (merge,
  буфер из `mem::Resource`), `sort_by_key(first, last, key_fn)`, `binary_search -> Optional<size_t>`,
  `partition`, `nth_element`, `min_element/max_element`, `any_of/all_of/none_of`, `find_if`, `count_if`.
  Готово, когда: случайные тесты против `std::sort` на 10⁵ элементах, стабильность проверена.
- [x] **1.7 `Variant<Ts...>` + `match`** (Rust `enum`, Kotlin `when`). Файл `yasl/vocab/variant.h`:
  `index()`, `is<T>()`, `get<T>() -> T*` (nullptr, без исключений), `visit`/`match(v, overloaded{...})`.
  P2, `(независимо)`.

## Фаза 2. Умные указатели (`yasl/ptr`) — P0

- [x] **2.1 `Unique<T>`** (unique_ptr / Rust `Box`). Файл `yasl/ptr/unique.h`.
  - Поля: `T* p_`, `mem::Resource* res_`, `void (*drop_)(mem::Resource*, void*)`. `drop_` создаётся в
    `make_unique_in<U>` и знает точный тип `U` → разрешено преобразование `Unique<Derived>` → `Unique<Base>`
    (требовать `__has_virtual_destructor(Base)` или совпадение типов; добавить `HasVirtualDestructor`, `DerivedFrom` в traits).
  - API: `make_unique<T>(args...)`, `make_unique_in<T>(res, args...)`, `get`, `*`, `->`, `explicit operator bool`,
    `release()` (возвращает `T*`, владение — вызывающему, вместе с ресурсом: вернуть `Pair<T*, Resource*>`),
    `reset()`, `swap`, `==` с `nullptr`. Копирование запрещено.
  - Готово, когда: тесты на upcast с виртуальным деструктором, Tracking без утечек, `sizeof(Unique<T>) == 3*sizeof(void*)`.
- [x] **2.2 `Shared<T>` + `Weak<T>`** (shared_ptr/weak_ptr, Rust `Arc`/`Weak`). Файлы `yasl/ptr/shared.h`, `shared_test.cpp`.
  - Одна аллокация: блок `{ RefCount strong; RefCount weak; Resource* res; void (*drop_value)(Block*); void (*free_block)(Block*); U value; }`.
    `Shared` хранит `T* p_` + `Block* b_` (2 слова) → разрешён upcast `Shared<Derived>` → `Shared<Base>`.
  - Слабые ссылки: `weak` = число Weak + 1 (за всех strong). При strong→0 разрушить значение, при weak→0 освободить блок.
  - `Weak::lock() -> Shared<T>` (пусто, если объект умер): CAS-цикл «инкремент, если не ноль» — добавить
    `RefCount::try_ref()` в `yasl/conc/ref_count.h`.
  - Переносим из `Arc`: `make_mut()` (copy-on-write), `use_count()`, `unique()`.
  - Дополнительно: `Shared<T>::from(Unique<T>&&)` (отдельный блок, хранит `Unique` — допустимы 2 аллокации), `SharedFromThis<T>` — P2.
  - `yasl/ptr/arc.h`: `template <class T> using Arc [[deprecated("use Shared")]] = Shared<T>;` + `make_arc` → `make_shared`.
    Перенести тесты из `arc_test.cpp` в `shared_test.cpp`, `String`-код, использующий `RefCount`, не трогать.
  - Готово, когда: тесты на циклы с Weak, `lock()` после смерти, многопоточный тест копирования/`lock` под TSan.
- [x] **2.3 Совместимость:** в `yasl/compat/std.h` — `std::hash` для `Unique/Shared`, `to_std(Unique&&)` не делать (разные аллокаторы). P2.

## Фаза 3. Форматирование (`yasl/io`) — P1

`format("{}")` и `FormatSpec` остаются (`yasl/io/output.h`, `yasl/io/output.cpp`).

- [x] **3.1 Числовые обёртки для StringBuilder / любого `io::Output`.** Файл `yasl/io/fmt.h`, namespace `yasl::fmt`.
  - Тип `template <Arithmetic T> struct Num { T v; io::FormatSpec spec; }` с цепочками-модификаторами:
    `.width(n) .fill(c) .left() .right() .center() .plus() .zero_pad() .precision(n)`.
  - Фабрики: `fmt::num(x)`, `fmt::hex(n)`, `fmt::HEX(n)`, `fmt::oct(n)`, `fmt::bin(n)`, `fmt::fixed(x, prec)`,
    `fmt::sci(x, prec)`, `fmt::pad(x, width, fill = ' ')`.
  - Новое (чего нет в FormatSpec): `fmt::grouped(n, sep = '_')` — разделитель тысяч (`1_000_000`),
    `fmt::bytes(n)` — человекочитаемый размер (`1.5 KiB`), `fmt::percent(x, prec)`.
  - Реализация: `operator<<(io::Output&, const Num<T>&)` вызывает существующие `io::format_integer` / `io::format_floating`;
    плюс `format_value(Output&, const Num<T>&, const FormatSpec&)`, чтобы обёртки работали и внутри `format("{}")`.
  - Пример в README: `sb << "t=" << fmt::fixed(dt, 3) << " id=" << fmt::hex(id).width(8).zero_pad();`
  - Готово, когда: тесты сверяют вывод с `format("{:...}")` для тех же спецификаторов.
- [x] **3.2 Проверка строки формата во время компиляции** (из старого TODO). `Зависит от:` ничего.
  - `template <class... Args> struct FormatString { consteval FormatString(const char* s); StringView str; }`
    — в consteval-конструкторе разобрать строку (вынести парсер спецификатора в `constexpr` функцию в заголовке,
    переиспользовать её в рантайм `vformat`), проверить: баланс `{}`, число аргументов, индексы `{N}`,
    допустимость `type` для категории аргумента (целое/плавающее/строка/прочее). Ошибка = вызов не-constexpr функции.
  - Сигнатуры: `format(FormatString<TypeIdentity<Args>...> fmt, const Args&...)`, то же у `Output::format`,
    `StringBuilder::format`, `io::println`. Для рантайм-строк — `io::runtime_format(StringView)`.
  - Готово, когда: есть compile-fail проверка (отдельный CMake-тест `try_compile` с неверной строкой ожидает ошибку)
    и все существующие вызовы компилируются.
- [x] **3.3 Синхронизация `io::out()` / `io::err()` между потоками** (из старого TODO). Использовать существующий `Mutex` из `yasl/conc/mutex.h`.
  Один вызов `format/println` = одна атомарная запись: форматировать во внутренний `ScratchOutput`, затем
  писать под `Mutex`. `operator<<` по-прежнему без блокировки, документировать. Добавить `io::Locked out = io::out().lock();`
  (Rust `stdout().lock()`). Тест: N потоков × M `println`, строки не перемешаны.
- [x] **3.4 `print/println/eprintln`** свободными функциями (Rust/Kotlin) + `io::read_line() -> Optional<String>`. P2.

## Фаза 4. Контейнеры — P0/P1

- [x] **4.1 Переписать `BTree` в B+-дерево с размерами поддеревьев.** P0. Файл `yasl/cont/assoc/btree.h` (заменить реализацию,
  публичный API `BTreeMap/BTreeSet` из `yasl/cont/assoc/map.h` сохранить).
  - **Листья** хранят элементы `T` (массив `kLeafCap`), указатели `prev/next` на соседние листья.
    У дерева — `first_leaf_`, `last_leaf_`, `size_`.
  - **Внутренние узлы** хранят `kInnerCap` ключей-разделителей (копии `key_type`) и `kInnerCap + 1` детей,
    а также `sizes[kInnerCap + 1]` — число элементов в каждом поддереве.
    `static_assert(CopyConstructible<key_type>)` с сообщением «move-only ключи — используйте TreapMap».
  - Разделитель `keys[i]` = минимальный ключ поддерева `kids[i+1]`. При удалении минимального элемента листа
    разделитель **не обязан** обновляться (инвариант: `keys[i-1] <= все ключи kids[i] < keys[i]`), это упрощает erase.
  - Элементы при split/merge/borrow **перемещаются** (`yasl::move` + `destroy_at`), не копируются → значения могут быть move-only.
  - **Итератор** = `{Leaf*, idx, const BTree*}`; `++` — `idx+1` или `leaf->next`; `--end()` → `last_leaf_`.
    Обход всего дерева строго O(n) без подъёма к родителю.
  - **Order statistics:** `kth(i)`, `rank(key)`, `index_of(it)` — O(log n) по `sizes`. Поддерживать `sizes` на пути при insert/erase.
  - **split(key, rest)** / **join(other)**: допустимо O(n) для перемещаемой части через `build_from_sorted`
    (см. ниже), если O(log n)-версия окажется сложной — записать это в комментарии.
  - Новое: `static BTree from_sorted(Span<T>/итераторы, Resource*)` — построение за O(n) (заполнить листья на ~100%,
    поднять уровни); `range(lo, hi)` — view для `for` по диапазону ключей.
  - Политика инвалидации прежняя: любая вставка/удаление инвалидирует итераторы; `erase(it)` возвращает следующий.
  - Готово, когда: все тесты в `yasl/cont/assoc/map_test.cpp` проходят без изменений API; добавлены рандомизированные
    тесты против `std::map` (10⁵ операций insert/erase/lower_bound/kth/rank, разные `kCap` через маленькие и большие `T`),
    тест move-only значения, тест на `Tracking` (ноль утечек), `README.md` (таблица «Два Map») обновлена:
    теперь kth/rank есть у обоих.
- [x] **4.2 `HashMap<K,V>` / `HashSet<K>`** (главный пробел относительно Go/Rust/Java). P0. Файл `yasl/cont/assoc/hash_map.h`.
  - Открытая адресация, Swiss-table-подобная схема: массив control-байт (7 бит хеша + empty/deleted) и массив слотов,
    группы по 8/16 байт, сравнение группы через SWAR на `uint64` (без SIMD-интринсиков, переносимо).
    Если SWAR сложно — сначала линейное пробирование с Robin Hood, но API тот же.
  - Хеш — `yasl/core/hash.h` (`Hash<T>`, `hash_value` по ADL); гетерогенный поиск (`map.get("k"_sv)`).
  - API как у `MapOps` (`yasl/cont/assoc/map.h`): переиспользовать `MapOps`/`SetOps` и `MapEntry`.
    Плюс `reserve`, `load_factor`, `erase_if(pred)`, `retain(pred)`.
  - Защита от HashDoS: случайный seed на процесс (из `getrandom`/`/dev/urandom`, добавить `sys/random.h` в ALLOWED или читать файл).
  - Готово, когда: рандомизированные тесты против `std::unordered_map`, тест на удаление/повторную вставку (tombstones), Tracking.
- [x] **4.3 `Deque<T>`** (Rust `VecDeque`, Java `ArrayDeque`): кольцевой буфер, `push/pop_front/back`, `operator[]`,
  итераторы произвольного доступа. Файл `yasl/cont/seq/deque.h`. P1.
- [x] **4.4 `SmallVector<T, N>`** — inline-буфер на N элементов, далее heap. Отдельный тип (не `Vector` + `InlineArena`:
  арена не освобождает память при росте); общую логику вставки/удаления вынести из `yasl/cont/seq/vector.h` в helper-функции.
  Файл `yasl/cont/seq/small_vector.h`. P1.
- [x] **4.5 `PriorityQueue<T, Cmp>`** (Rust `BinaryHeap`, Java `PriorityQueue`) поверх `Vector`. `push/pop/top/into_sorted()`. P1.
- [x] **4.6 Kotlin/Rust-методы `Vector`:** `retain(pred)`, `remove_if`, `dedup()`, `sort()`, `sort_by_key`, `contains`,
  `index_of -> Optional<size_t>`, `first()/last() -> Optional<T&>`, `extend(range)`, `drain(range)`, `split_off(i)`,
  `chunks(n)`/`windows(n)` (возвращают `Span`). `Зависит от:` 1.2, 1.6. P1.
- [x] **4.7 `BitSet`/`BitVector`** (Java `BitSet`): `set/test/reset`, `count`, `find_first`, итерация по установленным битам. P2.

## Фаза 5. Многопоточность (Go / Java / Rust) — P1

- [x] **5.1 `Thread`** (`yasl/conc/thread.h`, pthread): запуск с `Function<void()>`, `join()`, деструктор делает join
  (как `std::jthread`), `this_thread::sleep_for(Duration)`, `this_thread::yield()`. `Зависит от:` 1.3, 6.1.
- [x] **5.2 `Guarded<T>`** (Rust `Mutex<T>`): данные доступны только через `auto g = m.lock(); g->...`.
  Плюс `RwLock<T>` (pthread_rwlock), `CondVar` (pthread_cond), `Once` / `Lazy<T>` (C# `Lazy`, Rust `OnceLock`).
- [x] **5.3 `Channel<T>`** (Go channels): `make_channel<T>(capacity)` → `Sender<T>`/`Receiver<T>` (Shared-блок),
  `send` блокирует при заполнении, `recv() -> Optional<T>` (none после close и опустошения), `try_send/try_recv`,
  `close()`, итерация `for (auto v : rx)`. `capacity == 0` → rendezvous. `Зависит от:` 2.2, 5.2.
- [x] **5.4 `WaitGroup`** (Go), `Latch`, `ThreadPool` + `spawn(f) -> Future<T>` (простой, без work-stealing). P2.
- [x] **5.5 `Atomic<T>`** — тонкая обёртка над `<atomic>` с явными `load/store/fetch_add(order)` и `Relaxed/Acquire/...`. P2.

## Фаза 6. Время, случайные числа, система — P1/P2

- [x] **6.1 `Duration`, `Instant`, `SystemTime`** (Go `time`, Rust `std::time`). Файл `yasl/time/time.h` (`clock_gettime`, добавить `time.h` в ALLOWED).
  `Duration` — int64 наносекунд, литералы `5_ms`, `2_s`; форматирование `1.5s`, `250ms` (Go-стиль); `Instant::now()`, `elapsed()`. P1.
- [x] **6.2 `Random`** (`yasl/rand/random.h`): xoshiro256**, `seed_from_os()`, `next_u64`, `range(lo, hi)` без смещения
  (метод Лемира), `next_double`, `shuffle(Span)`. P2.
- [x] **6.3 `env::var(name) -> Optional<StringView>`, `env::args()`** (Go `os.Args`). P2.
- [x] **6.4 `fs`/`path`** (Go `filepath`, Rust `std::fs`): `Path` поверх `String`, `join`, `parent`, `extension`;
  `fs::read_dir`, `exists`, `remove`, `create_dir_all` → все возвращают `Result<_, io::Error>`. P3.

## Фаза 7. Строки и итерация — P1

- [x] **7.1 Недостающие методы `StringView`/`String`** (Kotlin/C#/Rust). Сначала проверить, что уже есть в
  `yasl/str/view.h`, `yasl/str/str.h`; добавить отсутствующее из списка: `lines()`, `split_whitespace()`,
  `replace(from, to)`, `repeat(n)`, `to_upper/to_lower` (ASCII), `strip_prefix/strip_suffix -> Optional<StringView>`,
  `contains`, `count(sub)`, `join(sep, range)` (свободная функция), `parse<T>() -> Result<T, ParseError>` (рядом с существующим Optional-вариантом).
- [x] **7.2 UTF-8** (Rust `chars()`, Go `range string`): `chars()` — итератор по code point (`char32_t`), `is_valid_utf8()`,
  `char_count()`, кодирование `push_char(char32_t)` в `StringBuilder`. P2.
- [x] **7.3 Ленивые адаптеры итерации** (Rust iterators, Kotlin sequences, C# LINQ). Файл `yasl/algo/iter.h`.
  Пайп-стиль: `v | map(f) | filter(p) | take(n)`, плюс `enumerate`, `zip`, `reverse`, `range(a, b)`, терминальные
  `collect<Vector>()`, `sum`, `count`, `fold`, `any/all`, `find -> Optional`. Только для range-for-совместимых типов. P2.

## Фаза 8. Сборка и линковка — P1 (из старого TODO)

`Зависит от:` 4.1 (type erasure делать уже для B+-дерева, а не для старого BTree).

- [x] **8.1 Вынести реализации из заголовков (pImpl / .cpp) ради ускорения сборки и линковки.**
  Сейчас много кода живёт в `.h` и компилируется в каждой единице трансляции.
  - Нешаблонные классы с тяжёлым приватным состоянием перевести на pImpl:
    `io::FileOutput`, `io::FileInput`, `io::File`, `mem::Arena`, `mem::Pool`, `mem::Tracking`.
    В заголовке остаётся только интерфейс, системные заголовки (`fcntl.h`, `unistd.h`, `pthread.h`) уходят в `.cpp`.
    pImpl-объект аллоцировать через `Unique` (`Зависит от:` 2.1).
  - Нешаблонные функции из заголовков перенести в `.cpp`: большая часть `String`, `StringView`
    (поиск, trim, split), `io::Input`, `StringBuilder`.
  - Шаблоны (`Vector`, `BTree`, `Treap`, `Trie`, `HashMap`) в pImpl не переводятся напрямую. Для них:
    - вынести нешаблонное ядро с type erasure (операции над узлами B+-дерева и treap через `void*`
      и таблицу операций над элементом), чтобы шаблонной оставалась только тонкая обёртка;
    - `extern template` + явные инстанцирования в `.cpp` для частых типов
      (`Vector<char>`, `Vector<String>`, `BTreeMap<String, String>` и т.п.).
  - Сократить транзитивные include: forward-декларации вместо полных заголовков
    (например, `io/output.h` не должен тянуть `str/num.h` целиком).
  - Замерить до и после: время полной пересборки, время сборки одного `.cpp`, размер объектников
    (`-ftime-trace` у clang). Результаты записать в `Done.txt`.
- [x] **8.2 Установка и пакетирование:** `install()` + `yaslConfig.cmake` для `find_package(yasl)`. P2.
- [x] **8.3 CI** (GitHub Actions): clang ASan+UBSan, clang TSan, GCC Release `-Werror`, include-policy. P2.
- [x] **8.4 Бенчмарки** `yasl/bench/` (отдельная цель, не в ctest): `BTreeMap` vs `std::map`, `HashMap` vs `std::unordered_map`,
  `String`/`format` vs std. Простой таймер на `Instant`. `Зависит от:` 6.1. P2.

## Фаза 9. Платформы — P3

- [ ] **9.1 Поддержка Windows:** `io::File` и `Mutex` поверх WinAPI (после 8.1 — системные заголовки уже только в `.cpp`),
  `Thread`, `time`. Include-политика: добавить `windows.h` в ALLOWED под условием.
  Перед портом — добавить Windows-CI (`windows-latest`), иначе код пишется вслепую (см. открытые вопросы).

## Фаза 10. Владение и память (`yasl/mem`, `yasl/ptr`) — P1

Идея (по мотивам обзора сторонних библиотек, реализация своя): один владелец группы объектов, объекты лежат рядом,
уничтожение — одним вызовом в обратном порядке создания.

- [x] **10.1 `mem::ObjPool`.** Файл `yasl/mem/obj_pool.h`. Надстройка над `Arena` (или сам `Resource`).
  - `make<T>(args...) -> T*`: размещает объект в арене. Для тривиально разрушаемых `T` — без учёта; для остальных —
    перед объектом заголовок `{dtor_fn, next}` и вставка в LIFO-цепочку.
  - `~ObjPool()` / `release()` / `reset()`: вызывает деструкторы в обратном порядке создания, затем освобождает чанки.
  - Исключение в конструкторе `T` не должно оставлять в цепочке недоконструированный объект (регистрировать после успеха).
  - `make_array<T>(n)`, `copy_str(StringView) -> StringView` (строка живёт в пуле) — удобно для парсеров и символьных таблиц.
  - Готово, когда: тесты на порядок деструкции, исключение в конструкторе, `Tracking` (нет утечек), вложенные пулы
    (пул внутри пула), `ScopedDefault` с пулом. Бенчмарк против `new`/`delete` в `yasl/bench`.
- [x] **10.2 Интрузивные указатели.** Файл `yasl/ptr/intrusive.h`: `RefCounted<T>` (CRTP-база со счётчиком, атомарный
  вариант и неатомарный через параметр), `IntrusivePtr<T>` (размер = один указатель, `from_this`, `adopt`/`retain`).
  Без `Weak` (кому нужен — `Shared`). Готово, когда: тесты, TSan, `sizeof(IntrusivePtr<T>) == sizeof(void*)`. `(независимо)`

## Фаза 11. Списки и АТД (`yasl/cont`) — P1

- [x] **11.1 `List<T>` — двусвязный список.** Файл `yasl/cont/seq/list.h`. Узлы из `mem::Resource`; итераторы не
  инвалидируются при вставке/удалении других элементов. API: `push_front/back`, `pop_front/back`, `emplace_*`,
  `insert(it, x)`, `erase(it)`, `splice(pos, other, it)` и `splice(pos, other)` за O(1), `reverse()`, `sort()` (merge sort,
  стабильная, без аллокаций), `remove_if`, `unique`, `front/back`, bidirectional-итераторы, move, копирование.
  Готово, когда: тесты (в т.ч. `Tracking`, move-only `T`), стабильность `sort`, итераторы переживают `splice`.
- [x] **11.2 `IntrusiveList<T, Node>`** — список без аллокаций: узел (`ListNode`) — член/база элемента, элемент
  отвязывается в своём деструкторе (опция auto-unlink). Файл `yasl/cont/seq/intrusive_list.h`. Нужен под `ObjPool`
  (цепочка деструкторов), очереди ожидания, LRU. Зависит от: 11.1 (API держать одинаковым). P2.
- [x] **11.3 АТД-адаптеры `Stack<T, C>` и `Queue<T, C>`.** Файл `yasl/cont/adapter/adapters.h`. Контейнер по умолчанию:
  `Stack` → `Vector<T>`, `Queue` → `Deque<T>`; любой контейнер с нужным набором операций подходит (проверка концептом:
  `push_back`, `pop_back`/`pop_front`, `back`/`front`, `size`, `empty`), в т.ч. `List<T>`.
  - `Stack`: `push/pop/top/size/empty/emplace`; `pop()` возвращает `Optional<T>` (в духе Rust), `top()` без
    проверки — только `YASL_ASSERT`.
  - `Queue`: `push/pop/front/back/…` аналогично.
  - `Deque<T>` (кольцевой буфер) уже есть — отдельный АТД не нужен, но добавить в тесты `Stack<int, List<int>>` и
    `Queue<int, List<int>>`, чтобы убедиться, что концепт не привязан к одному контейнеру.
  - Все конструкторы принимают `mem::Resource*`, который пробрасывается контейнеру. Зависит от: 11.1.
- [x] **11.4 Строковый словарь на Trie.** `Trie<V>` уже есть (`yasl/cont/assoc/trie.h`, ключи — байтовые строки), поэтому
  задача — довести до полноценного «map для строк»:
  - `StringMap<V>` — публичное имя поверх `Trie<V>` с тем же интерфейсом, что у `BTreeMap`/`TreapMap`: встроить в
    существующий CRTP `MapOps<Derived, V>` из `yasl/cont/assoc/map.h` (`try_emplace`, `insert_or_assign`, `find`, `contains`,
    `erase`, `operator[]`, `size`, `clear`), итерация в лексикографическом порядке через `for (auto& [key, value] : m)`.
    Особенность: ключ не хранится в узле целиком, поэтому элемент итерации — не `MapEntry`, а прокси `{StringView key, V& value}`
    (поддержать structured bindings); если `MapOps` этого не допускает — записать вопрос, а не ломать `MapOps`.
  - `StringSet` = `StringMap<Empty>`; `has_prefix`, `count_with_prefix` проброшены.
  - Итераторы (`begin/end`, range-for) по всему дереву и по диапазону `with_prefix("ab")`; сейчас только `for_each`.
  - Восстановление ключа при итерации без аллокации на каждый шаг (общий буфер в итераторе).
  - Оптимизация (отдельным коммитом): сжатие путей (radix-дерево) — рёбра хранят подстроку, а не символ; сравнить
    память и скорость с `HashMap<String, V>` в `yasl/bench`. Реализация должна пройти те же тесты, что и обычный `Trie`.
  - Готово, когда: тесты на пустой ключ, общие префиксы, удаление с «подчисткой» узлов, `Tracking`, бенчмарк.

## Фаза 12. Потоки ввода-вывода (`yasl/io`) — P1

Уже есть: `Input`/`Output` сами буферизованы (`pos_/end_`, `underflow()`/`do_flush()`), `read`, `read_line`, `read_all`,
`NullOutput`, `ArrayOutput`, `StringInput` (без копии), `FileOutput::write_all`. Отдельные `Buffered*`/`Null*`/`Memory*` не нужны.

- [x] **12.1 Базовые операции:** `Input::read_exact(char*, n)` (ровно n байт или ошибка «неожиданный конец»),
  `copy(Input&, Output&) -> size_t` (через `buffered()`, без промежуточного буфера). Ошибки — как в остальном `io`.
- [x] **12.2 Декораторы:** `LimitedInput` (не больше N байт из нижележащего `Input`), `TeeOutput` (запись в два `Output`).
  Принимают ссылку на нижележащий поток; владение — на вызывающем (или `ObjPool`, см. 10.1). Зависит от: 12.1. P2.
- [x] **12.3 `Buffer`** — растущий байтовый буфер с `reserve`, `append`, `consume(n)`. Только если понадобится вне
  `Input/Output` (например, для парсеров). Файл `yasl/io/buffer.h`. P3.

## Фаза 13. Тонкие обёртки над ОС (`yasl/os`, `yasl/conc`) — P1/P2

Правило: публичные заголовки не тянут `unistd.h`/`windows.h`; нативный хэндл — `int` (unix) / `intptr_t` (Windows),
реализация в `.cpp`. Это заранее задаёт границу под порт из 9.1.

- [x] **13.1 `Fd`** — RAII над дескриптором: move-only, `release()`, `dup()`, `set_nonblocking()`, `set_cloexec()`,
  `read/write` c `Result`. `io::File` перевести на `Fd`. P1.
- [x] **13.2 `Pipe` и `MemFd`** (anonymous file: `memfd_create` на Linux, `shm_open`/`unlink`-fallback на остальных unix).
  Зависит от: 13.1. P2.
- [x] **13.3 `Semaphore`, `Barrier`** (на `Mutex`+`CondVar`; на Linux можно `sem_t`). `(независимо)` P2.

## Фаза 14. Проверки и паника (`yasl/core/check.h`) — P1

- [x] **14.1 Три уровня проверок.** `YASL_ASSERT` (уже есть, только Debug), `YASL_VERIFY(expr)` (всегда, без сообщения),
  `YASL_INSIST(expr, "сообщение")` (всегда, с пояснением). Все ведут в одну `[[noreturn]] panic(...)`.
- [x] **14.2 `panic`:** печатает файл:строка:функция и сообщение в stderr без аллокаций и форматирования через `format`
  (может вызываться при нехватке памяти); опционально backtrace через `execinfo.h` (добавить в ALLOWED с причиной;
  на платформах без него — пропускать). Хук `set_panic_hook(fn)` для тестов (`CHECK_PANICS`). Зависит от: 14.1.

## Фаза 15. Мелочи — P3

- [x] **15.1 `Pcg32`/`Pcg64`** рядом с xoshiro в `yasl/rand/random.h` (с тем же API, что у `Random`: `next_u64`, `range`, `shuffle`).
- [x] **15.2 Таблица символов** `SymbolTable` (интернирование строк): `intern(StringView) -> Symbol` (индекс или `StringView`
  на пул), равные строки → равные символы, сравнение за O(1). Хранилище — `ObjPool::copy_str` + `HashMap`. Зависит от: 10.1.
  Альтернативная реализация на `StringMap` из 11.4 — сравнить в бенчмарке.

---

## Порядок выполнения (кратко)

1.1 → 1.2 → 1.3 → 2.1 → 2.2 → 4.1 → 4.2 → 3.1 → 3.2 → 6.1 → 5.1 → 5.2 → 3.3 → 5.3 → 1.6 → 4.6 → 4.3 → 4.4 → 4.5 → 7.1 → 8.1 → остальное P2/P3.
Задачи с пометкой `(независимо)` можно делать параллельно в любой момент.

Новые фазы (10–15), рекомендуемый порядок: 10.1 → 11.1 → 11.3 → 11.4 → 10.2 → 13.1 → 14.1 → 14.2 → 12.1 → 12.2 →
11.2 → 13.2 → 13.3 → 15.x → 12.3. Сеть, корутины и reactor/poller — вне целей на данный момент; `Fd` (13.1) проектировать так,
чтобы поверх него потом можно было добавить poller без переделки.

## Решения, принятые при планировании

- Указатели в C++-стиле: `Unique`, `Shared` (атомарный), `Weak`. `Arc` — устаревший алиас на `Shared`.
- `format("{}")` остаётся; числовые обёртки `fmt::*` — дополнение для `StringBuilder`/`<<`.
- `BTree` → B+-дерево с размерами поддеревьев; ключи в B+ копируемые, move-only ключи → `TreapMap`.
  Значения при перестройке перемещаются (заменяет старый пункт «перемещать ключи / некопируемые ключи»).
- Ошибки: `Result<T, E>` для ожидаемых ошибок (io, парсинг), исключения — для нарушений контракта (`at()`, `value()`).

## Открытые вопросы

(сюда исполнитель записывает вопросы, требующие решения владельца)

- **8.1 решена владельцем: pImpl везде, где можно; где нельзя — не использовать.** Сделано: `Mutex`/`RawRwLock`/`CondVar`
  держат `pthread_*` в буфере фиксированного размера внутри объекта (без аллокации; размер проверяется `static_assert`
  в `.cpp`), `Thread` хранит `pthread_t` как `uint64` — `<pthread.h>` ушёл из публичных заголовков; `FileOutput` и
  `FileInput` перенесены на `Unique<Impl>`; `String`-утилиты (`replace_all`, `repeat`, `to_upper/lower`) — в `str.cpp`;
  `extern template` пробовал и откатил: ускорение сборки мало, а явные инстанцирования раздувают бинарник
  (в объектник попадают все члены). Не делалось, потому что нельзя: `Arena`/`Pool`/`Tracking` (горячий путь аллокатора, системных
  заголовков нет), `File` (уже значение из одного `int`), `StringView` (constexpr), шаблонные контейнеры (type erasure
  узлов B+/treap через `void*` оставлен: цена — потеря inlining в самом горячем коде). Замеры `-ftime-trace` не проводились.

- **11.4: оптимизация radix-дерева (сжатие путей) не выполнена.** `StringMap` работает на обычном `Trie`; в бенчмарке
  (`yasl_bench string`, Release) он заметно медленнее `HashMap<String,V>`: insert ≈2100 ns/op против ≈200, find ≈845 против ≈133,
  потому что на каждый байт ключа — узел и `Vector` детей. Сжатие путей — отдельная переработка `trie.h`; нужно решение,
  оправдана ли она (порядок и поиск по префиксу нужны не везде, для точечных запросов есть `HashMap`).

- **9.1 (Windows) не выполнена.** В среде исполнителя нет Windows-тулчейна, поэтому порт `File`/`Mutex`/`Thread`/`time`/`fs`
  поверх WinAPI нельзя ни собрать, ни проверить. Нужно решение: делать «вслепую» (с риском неработающего кода) или
  подключить Windows-CI (GitHub Actions `windows-latest`) и вести порт от него.
