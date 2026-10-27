# TODO — roadmap yasl

Цель: по максимуму закрыть слабые стороны std C++20 и взять лучшее из Rust, Go, C#, Java, Kotlin,
сохраняя главное правило — **ядро не использует стандартную библиотеку C++** (см. README).

## Правила для исполнителя (читать перед каждой задачей)

1. **Одна задача = один коммит.** Берите задачи по порядку фаз; внутри фазы — по порядку, если не указано `(независимо)`.
   Не начинайте задачу, пока её зависимости (`Зависит от:`) не отмечены `[x]`.
2. **Include-политика.** В ядре (`yasl/**`, кроме `yasl/compat/`, `*_test.cpp`, `yasl/test/`) разрешены только
   `<new> <initializer_list> <compare> <cstddef> <cstdint> <atomic>` и заголовки ОС из `cmake/check_includes.cmake`.
   Новый системный заголовок (например `<time.h>`) — добавить в `ALLOWED` в том же коммите и указать причину.
   Traits/концепты — только из `yasl/sys/traits.h` (на builtins); не хватает — добавить туда.
3. **Стиль — как в окружающем коде:** `namespace yasl` (подмодули `yasl::io`, `yasl::mem`, …), 4 пробела,
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

## Фаза 1. Базовые типы (`yasl/sys`) — P0

- [x] **1.1 `Result<T, E>`** (Rust `Result`, Kotlin `Result`). Файл `yasl/sys/result.h`.
  - Хранение: union + флаг, как в `yasl/sys/optional.h`. `Ok(v)` / `Err(e)` — фабрики-теги
    (`yasl::ok(v)`, `yasl::err(e)`), неявно конвертируются в любой `Result`.
  - API: `is_ok() is_err() explicit operator bool`, `value()` (бросает `BadAccess`), `operator* ->`,
    `error()`, `value_or(x)`, `map(f)`, `map_err(f)`, `and_then(f)`, `or_else(f)`, `ok() -> Optional<T>`,
    `Result<void, E>` (специализация).
  - Макрос `YASL_TRY(expr)`: на GCC/clang statement-expression `({ auto r = expr; if (!r) return err(...); *r; })`;
    + `YASL_TRY_VOID(expr)`.
  - Тип ошибки по умолчанию для io: `io::Error` (уже есть в `io/file.h`) — проверьте, что он копируемый.
  - Готово, когда: тесты на ok/err, move-only `T`, нетривиальные деструкторы (Tracking), `YASL_TRY` в цепочке.
- [x] **1.2 Комбинаторы `Optional`** (Rust `Option`, Kotlin `?.`/`?:`). Файл `yasl/sys/optional.h`.
  `map`, `and_then`, `or_else`, `filter`, `take()` (забрать и обнулить), `ok_or(e) -> Result`,
  `value_or_else(f)`, `Optional<T&>` (специализация: хранит указатель). `Зависит от:` 1.1 (для `ok_or`).
- [x] **1.3 `Function<Sig>` и `FunctionRef<Sig>`.** Файл `yasl/sys/function.h`.
  - `FunctionRef` — невладеющая (указатель на объект + thunk), для параметров-колбэков.
  - `Function` — владеющая, small-buffer 2–3 указателя, иначе аллокация через `mem::Resource`;
    поддержка move-only лямбд (как `std::move_only_function`), копирование запрещено.
  - Готово, когда: тесты SBO/heap, move-only захваты, отсутствие утечек.
- [x] **1.4 `defer` / `ScopeExit`** (Go `defer`). Файл `yasl/sys/defer.h`:
  `auto g = scope_exit([&]{...}); g.dismiss();` и макрос `YASL_DEFER { ... };`. `(независимо)`
- [x] **1.5 Проверенная арифметика** (Rust `checked_*`, `saturating_*`, `wrapping_*`). Файл `yasl/sys/math.h`:
  `checked_add/sub/mul -> Optional<T>` (через `__builtin_add_overflow`), `saturating_add/sub/mul`,
  `narrow<T>(x) -> Optional<T>` (C# `checked` cast), `div_ceil`, `is_pow2`, `popcount`, `countr_zero`.
  Перенести битовые утилиты из `utility.h` сюда, оставив `#include` для совместимости. `(независимо)`
- [x] **1.6 Сортировка и алгоритмы** (в std нет сортировки без `<algorithm>`). Файл `yasl/sys/algo.h`:
  `sort` (introsort: quicksort median-of-3 + heapsort fallback + insertion < 16), `stable_sort` (merge,
  буфер из `mem::Resource`), `sort_by_key(first, last, key_fn)`, `binary_search -> Optional<size_t>`,
  `partition`, `nth_element`, `min_element/max_element`, `any_of/all_of/none_of`, `find_if`, `count_if`.
  Готово, когда: случайные тесты против `std::sort` на 10⁵ элементах, стабильность проверена.
- [ ] **1.7 `Variant<Ts...>` + `match`** (Rust `enum`, Kotlin `when`). Файл `yasl/sys/variant.h`:
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
    `RefCount::try_ref()` в `yasl/atomic.h`.
  - Переносим из `Arc`: `make_mut()` (copy-on-write), `use_count()`, `unique()`.
  - Дополнительно: `Shared<T>::from(Unique<T>&&)` (отдельный блок, хранит `Unique` — допустимы 2 аллокации), `SharedFromThis<T>` — P2.
  - `yasl/ptr/arc.h`: `template <class T> using Arc [[deprecated("use Shared")]] = Shared<T>;` + `make_arc` → `make_shared`.
    Перенести тесты из `arc_test.cpp` в `shared_test.cpp`, `String`-код, использующий `RefCount`, не трогать.
  - Готово, когда: тесты на циклы с Weak, `lock()` после смерти, многопоточный тест копирования/`lock` под TSan.
- [ ] **2.3 Совместимость:** в `yasl/compat/std.h` — `std::hash` для `Unique/Shared`, `to_std(Unique&&)` не делать (разные аллокаторы). P2.

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
- [x] **3.3 Синхронизация `io::out()` / `io::err()` между потоками** (из старого TODO). Использовать существующий `Mutex` из `yasl/sys/mutex.h`.
  Один вызов `format/println` = одна атомарная запись: форматировать во внутренний `ScratchOutput`, затем
  писать под `Mutex`. `operator<<` по-прежнему без блокировки, документировать. Добавить `io::Locked out = io::out().lock();`
  (Rust `stdout().lock()`). Тест: N потоков × M `println`, строки не перемешаны.
- [ ] **3.4 `print/println/eprintln`** свободными функциями (Rust/Kotlin) + `io::read_line() -> Optional<String>`. P2.

## Фаза 4. Контейнеры — P0/P1

- [x] **4.1 Переписать `BTree` в B+-дерево с размерами поддеревьев.** P0. Файл `yasl/map/btree.h` (заменить реализацию,
  публичный API `BTreeMap/BTreeSet` из `yasl/map/map.h` сохранить).
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
  - Готово, когда: все тесты в `yasl/map/map_test.cpp` проходят без изменений API; добавлены рандомизированные
    тесты против `std::map` (10⁵ операций insert/erase/lower_bound/kth/rank, разные `kCap` через маленькие и большие `T`),
    тест move-only значения, тест на `Tracking` (ноль утечек), `README.md` (таблица «Два Map») обновлена:
    теперь kth/rank есть у обоих.
- [x] **4.2 `HashMap<K,V>` / `HashSet<K>`** (главный пробел относительно Go/Rust/Java). P0. Файл `yasl/map/hash_map.h`.
  - Открытая адресация, Swiss-table-подобная схема: массив control-байт (7 бит хеша + empty/deleted) и массив слотов,
    группы по 8/16 байт, сравнение группы через SWAR на `uint64` (без SIMD-интринсиков, переносимо).
    Если SWAR сложно — сначала линейное пробирование с Robin Hood, но API тот же.
  - Хеш — `yasl/sys/hash.h` (`Hash<T>`, `hash_value` по ADL); гетерогенный поиск (`map.get("k"_sv)`).
  - API как у `MapOps` (`yasl/map/map.h`): переиспользовать `MapOps`/`SetOps` и `MapEntry`.
    Плюс `reserve`, `load_factor`, `erase_if(pred)`, `retain(pred)`.
  - Защита от HashDoS: случайный seed на процесс (из `getrandom`/`/dev/urandom`, добавить `sys/random.h` в ALLOWED или читать файл).
  - Готово, когда: рандомизированные тесты против `std::unordered_map`, тест на удаление/повторную вставку (tombstones), Tracking.
- [x] **4.3 `Deque<T>`** (Rust `VecDeque`, Java `ArrayDeque`): кольцевой буфер, `push/pop_front/back`, `operator[]`,
  итераторы произвольного доступа. Файл `yasl/vec/deque.h`. P1.
- [x] **4.4 `SmallVector<T, N>`** — inline-буфер на N элементов, далее heap. Отдельный тип (не `Vector` + `InlineArena`:
  арена не освобождает память при росте); общую логику вставки/удаления вынести из `yasl/vec/vector.h` в helper-функции.
  Файл `yasl/vec/small_vector.h`. P1.
- [x] **4.5 `PriorityQueue<T, Cmp>`** (Rust `BinaryHeap`, Java `PriorityQueue`) поверх `Vector`. `push/pop/top/into_sorted()`. P1.
- [x] **4.6 Kotlin/Rust-методы `Vector`:** `retain(pred)`, `remove_if`, `dedup()`, `sort()`, `sort_by_key`, `contains`,
  `index_of -> Optional<size_t>`, `first()/last() -> Optional<T&>`, `extend(range)`, `drain(range)`, `split_off(i)`,
  `chunks(n)`/`windows(n)` (возвращают `Span`). `Зависит от:` 1.2, 1.6. P1.
- [ ] **4.7 `BitSet`/`BitVector`** (Java `BitSet`): `set/test/reset`, `count`, `find_first`, итерация по установленным битам. P2.

## Фаза 5. Многопоточность (Go / Java / Rust) — P1

- [x] **5.1 `Thread`** (`yasl/sys/thread.h`, pthread): запуск с `Function<void()>`, `join()`, деструктор делает join
  (как `std::jthread`), `this_thread::sleep_for(Duration)`, `this_thread::yield()`. `Зависит от:` 1.3, 6.1.
- [x] **5.2 `Guarded<T>`** (Rust `Mutex<T>`): данные доступны только через `auto g = m.lock(); g->...`.
  Плюс `RwLock<T>` (pthread_rwlock), `CondVar` (pthread_cond), `Once` / `Lazy<T>` (C# `Lazy`, Rust `OnceLock`).
- [x] **5.3 `Channel<T>`** (Go channels): `make_channel<T>(capacity)` → `Sender<T>`/`Receiver<T>` (Shared-блок),
  `send` блокирует при заполнении, `recv() -> Optional<T>` (none после close и опустошения), `try_send/try_recv`,
  `close()`, итерация `for (auto v : rx)`. `capacity == 0` → rendezvous. `Зависит от:` 2.2, 5.2.
- [ ] **5.4 `WaitGroup`** (Go), `Latch`, `ThreadPool` + `spawn(f) -> Future<T>` (простой, без work-stealing). P2.
- [ ] **5.5 `Atomic<T>`** — тонкая обёртка над `<atomic>` с явными `load/store/fetch_add(order)` и `Relaxed/Acquire/...`. P2.

## Фаза 6. Время, случайные числа, система — P1/P2

- [x] **6.1 `Duration`, `Instant`, `SystemTime`** (Go `time`, Rust `std::time`). Файл `yasl/sys/time.h` (`clock_gettime`, добавить `time.h` в ALLOWED).
  `Duration` — int64 наносекунд, литералы `5_ms`, `2_s`; форматирование `1.5s`, `250ms` (Go-стиль); `Instant::now()`, `elapsed()`. P1.
- [x] **6.2 `Random`** (`yasl/sys/random.h`): xoshiro256**, `seed_from_os()`, `next_u64`, `range(lo, hi)` без смещения
  (метод Лемира), `next_double`, `shuffle(Span)`. P2.
- [x] **6.3 `env::var(name) -> Optional<StringView>`, `env::args()`** (Go `os.Args`). P2.
- [ ] **6.4 `fs`/`path`** (Go `filepath`, Rust `std::fs`): `Path` поверх `String`, `join`, `parent`, `extension`;
  `fs::read_dir`, `exists`, `remove`, `create_dir_all` → все возвращают `Result<_, io::Error>`. P3.

## Фаза 7. Строки и итерация — P1

- [x] **7.1 Недостающие методы `StringView`/`String`** (Kotlin/C#/Rust). Сначала проверить, что уже есть в
  `yasl/str/view.h`, `yasl/str/str.h`; добавить отсутствующее из списка: `lines()`, `split_whitespace()`,
  `replace(from, to)`, `repeat(n)`, `to_upper/to_lower` (ASCII), `strip_prefix/strip_suffix -> Optional<StringView>`,
  `contains`, `count(sub)`, `join(sep, range)` (свободная функция), `parse<T>() -> Result<T, ParseError>` (рядом с существующим Optional-вариантом).
- [ ] **7.2 UTF-8** (Rust `chars()`, Go `range string`): `chars()` — итератор по code point (`char32_t`), `is_valid_utf8()`,
  `char_count()`, кодирование `push_char(char32_t)` в `StringBuilder`. P2.
- [ ] **7.3 Ленивые адаптеры итерации** (Rust iterators, Kotlin sequences, C# LINQ). Файл `yasl/sys/iter.h`.
  Пайп-стиль: `v | map(f) | filter(p) | take(n)`, плюс `enumerate`, `zip`, `reverse`, `range(a, b)`, терминальные
  `collect<Vector>()`, `sum`, `count`, `fold`, `any/all`, `find -> Optional`. Только для range-for-совместимых типов. P2.

## Фаза 8. Сборка и линковка — P1 (из старого TODO)

`Зависит от:` 4.1 (type erasure делать уже для B+-дерева, а не для старого BTree).

- [ ] **8.1 Вынести реализации из заголовков (pImpl / .cpp) ради ускорения сборки и линковки.**
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
- [ ] **8.2 Установка и пакетирование:** `install()` + `yaslConfig.cmake` для `find_package(yasl)`. P2.
- [ ] **8.3 CI** (GitHub Actions): clang ASan+UBSan, clang TSan, GCC Release `-Werror`, include-policy. P2.
- [ ] **8.4 Бенчмарки** `yasl/bench/` (отдельная цель, не в ctest): `BTreeMap` vs `std::map`, `HashMap` vs `std::unordered_map`,
  `String`/`format` vs std. Простой таймер на `Instant`. `Зависит от:` 6.1. P2.

## Фаза 9. Платформы — P3

- [ ] **9.1 Поддержка Windows:** `io::File` и `Mutex` поверх WinAPI (после 8.1 — системные заголовки уже только в `.cpp`),
  `Thread`, `time`. Include-политика: добавить `windows.h` в ALLOWED под условием.

---

## Порядок выполнения (кратко)

1.1 → 1.2 → 1.3 → 2.1 → 2.2 → 4.1 → 4.2 → 3.1 → 3.2 → 6.1 → 5.1 → 5.2 → 3.3 → 5.3 → 1.6 → 4.6 → 4.3 → 4.4 → 4.5 → 7.1 → 8.1 → остальное P2/P3.
Задачи с пометкой `(независимо)` можно делать параллельно в любой момент.

## Решения, принятые при планировании

- Указатели в C++-стиле: `Unique`, `Shared` (атомарный), `Weak`. `Arc` — устаревший алиас на `Shared`.
- `format("{}")` остаётся; числовые обёртки `fmt::*` — дополнение для `StringBuilder`/`<<`.
- `BTree` → B+-дерево с размерами поддеревьев; ключи в B+ копируемые, move-only ключи → `TreapMap`.
  Значения при перестройке перемещаются (заменяет старый пункт «перемещать ключи / некопируемые ключи»).
- Ошибки: `Result<T, E>` для ожидаемых ошибок (io, парсинг), исключения — для нарушений контракта (`at()`, `value()`).

## Открытые вопросы

(сюда исполнитель записывает вопросы, требующие решения владельца)

- **8.1 (pImpl / вынос реализаций) не выполнена — нужно решение.** Пункты задачи плохо ложатся на код:
  `mem::Arena`, `mem::Pool` уже имеют реализацию в `.cpp`, в заголовках нет системных заголовков; pImpl добавил бы
  лишнюю косвенность на горячем пути аллокатора. `Tracking` — 56 строк в заголовке, без системных include.
  `io::File*` уже тянет `unistd.h/fcntl.h` только в `.cpp`; единственный системный заголовок в публичных
  заголовках — `pthread.h` (`Mutex`, `CondVar`, `Thread`, `RwLock`), и спрятать его можно только ценой аллокации
  на каждый мьютекс (или `alignas`-буфера с `static_assert` на размер). `StringView` почти целиком `constexpr`,
  переносить в `.cpp` нечего. Вопросы: (1) делать ли pImpl для `Mutex`/`CondVar` через буфер фиксированного размера?
  (2) ограничиться `extern template` для `Vector<char>/Vector<String>/BTreeMap<String,String>` (выигрыш мал, т.к.
  inline-методы всё равно инстанцируются)? (3) замерять ли сначала `-ftime-trace`, чтобы увидеть реальных виновников?
  Остальные задачи плана выполнялись дальше, 8.1 пропущена.
