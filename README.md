# yasl

Yet Another Standard Library для C++20. Контейнеры, строки, ввод-вывод, умные указатели,
примитивы для потоков, аллокаторы и ещё по мелочи. Ядро не включает заголовки стандартной
библиотеки C++, только то, без чего язык не обходится (`<new>`, `<initializer_list>`, `<compare>`,
`<cstddef>`, `<cstdint>`, `<atomic>`), и системные заголовки (`unistd.h`, `pthread.h`, `sys/mman.h` и так далее).
Это проверяет тест `yasl_include_policy` (`cmake/check_includes.cmake`). Код для работы вместе со std
лежит отдельно в `yasl/compat/`.

Платформа: Linux проверяется локально и в CI. Ветки для macOS и Windows написаны, но Windows ни разу не собиралась.

## Сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DYASL_SANITIZE=ON
cmake --build build -j
cd build && ctest
./build/yasl_tests btree      # только тесты, в имени которых есть "btree"
```

Опции CMake:

| Опция | По умолчанию | Что делает |
|---|---|---|
| `YASL_BUILD_TESTS` | `ON` для корневого проекта | собирает `yasl_tests` |
| `YASL_BUILD_BENCH` | `OFF` | собирает `yasl_bench` (собирайте в Release); нужен Abseil (`find_package(absl)`) |
| `YASL_SANITIZE` | `OFF` | ASan и UBSan |
| `YASL_MALLOC` | `libc` | системный аллокатор: `libc`, `tcmalloc` или `custom` |
| `YASL_DEFAULT_ALLOC` | `sys` | ресурс по умолчанию: `sys` или `chain` |

Использование из другого проекта:

```sh
cmake --install build --prefix /opt/yasl
```
```cmake
find_package(yasl REQUIRED)
target_link_libraries(app PRIVATE yasl::yasl)
```

### Другие системы сборки

CMake — основная сборка. Кроме неё есть Bazel, Meson и ya make (Аркадия) с теми же опциями:

| | Bazel | Meson | ya make |
|---|---|---|---|
| файлы | `MODULE.bazel`, `BUILD.bazel`, `.bazelrc` | `meson.build`, `meson.options` | `ya.make`, `yasl/test/ya.make` |
| аллокатор | `--//:malloc=libc\|tcmalloc\|custom` | `-Dmalloc=...` | `-DYASL_MALLOC=...` |
| ресурс по умолчанию | `--//:default_alloc=sys\|chain` | `-Ddefault_alloc=...` | `-DYASL_DEFAULT_ALLOC=...` |
| tcmalloc | `@tcmalloc` из BCR, сабмодуль не нужен | архив из `third_party/tcmalloc_build/build.sh` (`-Dtcmalloc_lib=...`) | `ALLOCATOR(TCMALLOC...)` в программе |
| подключение | `bazel_dep(name = "yasl")` + override, `@yasl//:yasl` | `dependency('yasl')` (subproject) или `pkg-config yasl` | `PEERDIR(contrib/libs/yasl)` |

```sh
bazel test //:yasl_tests                       # --config=tcmalloc: с google/tcmalloc
bazel run -c opt //:yasl_bench
meson setup build-meson -Dbuildtype=release && meson test -C build-meson
ya make -r contrib/libs/yasl                   # ожидает репозиторий в contrib/libs/yasl
```

Meson и ya.make перечисляют исходники явно. Тест `yasl_build_lists` (`cmake/check_build_lists.cmake`)
падает, если новый `.cpp` не добавлен в эти списки. ya.make написан по правилам Аркадии, но не собирался:
`ya` вне Аркадии недоступен.

Бенчмарки: `./build-bench/yasl_bench [--csv] [фильтр]`. Фильтр ищет подстроку в строке `группа имя`,
`--csv` выводит результаты в CSV. Группы: `alloc_bench.cpp` (аллокаторы), `cont_bench.cpp` (контейнеры),
`str_bench.cpp` (строки и числа). В каждой группе одна и та же работа разными реализациями: yasl, std,
Abseil. Чтобы сравнить с google/tcmalloc, соберите бенчмарки с `-DYASL_MALLOC=tcmalloc` и запускайте с
`GLIBC_TUNABLES=glibc.pthread.rseq=0` (см. раздел «Память»).

## Структура

| Каталог | Что там |
|---|---|
| `yasl/core` | типы, traits и концепты, `move`/`forward`/`swap`, `Span`, хеширование, исключения, проверки и паника (`YASL_ASSERT`, `YASL_VERIFY`, `YASL_INSIST`, `YASL_PANIC`), арифметика с проверкой переполнения |
| `yasl/vocab` | `Optional`, `Result`, `Variant`, `Function`, `FunctionRef`, `defer` |
| `yasl/mem` | ресурсы и аллокаторы, см. раздел «Память» |
| `yasl/ptr` | `Unique`, `Shared`, `Weak`, `IntrusivePtr` |
| `yasl/str` | `StringView`, `String`, `StringBuilder`, разбор и вывод чисел, `SymbolTable` |
| `yasl/cont/seq` | `Vector`, `SmallVector`, `Deque`, `List`, `IntrusiveList`, `BitSet` |
| `yasl/cont/assoc` | `HashMap`/`HashSet`, `BTreeMap`/`BTreeSet`, `TreapMap`/`TreapSet`, `Trie`, `RadixTrie`, `StringMap`/`StringSet` |
| `yasl/cont/adapter` | `Stack`, `Queue`, `PriorityQueue` |
| `yasl/algo` | `sort`, `stable_sort`, `binary_search`, `partition` и другие алгоритмы; ленивые адаптеры итерации (`iter.h`) |
| `yasl/io` | `Output`/`Input`, `format`, `println`, файлы, `Buffer`, `Path` и `fs` |
| `yasl/os` | `Fd`, `Pipe`, `MemFd`, `env` |
| `yasl/conc` | `Thread`, `Mutex`, `Guarded`, `RwLock`, `CondVar`, `Once`, `Semaphore`, `Barrier`, `Channel`, `ThreadPool`, `WaitGroup`, `Atomic` |
| `yasl/time` | `Duration`, `Instant`, `SystemTime` |
| `yasl/rand` | `Random` (xoshiro256**), `Pcg32`, `Pcg64` |
| `yasl/compat` | мост к std: `mem::Allocator<T>`, адаптер для `std::pmr`, `std::hash` для `Unique` и `Shared` |
| `yasl/test` | свой тестовый раннер |
| `yasl/bench` | бенчмарки |

Тесты лежат рядом с кодом, в файлах `*_test.cpp`.

## Основное по модулям

### Optional и Result

```cpp
Optional<int> x = 5;
x.map([](int v) { return v * 2; }).value_or(0);   // 10
x.and_then(f).or_else(g);
x.ok_or("нет значения");                          // Result<int, const char*>

Result<int, io::Error> r = parse();
if (r) use(*r);
r.map(f).map_err(g).value_or(0);

Result<size_t, io::Error> count_entries(const io::Path& dir) {
  auto entries = YASL_TRY(io::fs::read_dir(dir));    // при ошибке вернёт её из функции
  return entries.size();
}
```

### Строки

`String` не изменяется через `operator[]`: копирование дешёвое (общий буфер со счётчиком ссылок),
писать нужно через `set(i, c)` или `mut_data()`. Буфер всегда заканчивается нулём.

```cpp
String s = "hello";
s.append(", world");
s.find("wor");  s.substr(0, 5);  s.starts_with("he");
s.replace_all("l", "L");  s.to_upper();
auto parts = join(", ", names);

StringBuilder b;
b << "t=" << fmt::fixed(dt, 3) << " id=" << fmt::hex(id).width(8).zero_pad();
String out = b.take();
```

Для UTF-8 у `StringView` есть `chars()` (итератор по `char32_t`) и `is_valid_utf8()`.

### Контейнеры

`Vector`, `HashMap`, `BTreeMap`, `TreapMap` и `StringMap` имеют одинаковый набор базовых методов:
`try_emplace`, `insert_or_assign`, `find`, `get` (указатель на значение или `nullptr`), `contains`, `erase`, `operator[]`.

```cpp
Vector<int> v{3, 1, 2};
v.push_back(4);  v.sort();  v.retain([](int x) { return x > 1; });
v.dedup();  v.contains(2);

HashMap<String, int> m;
m["a"] = 1;
if (int* p = m.get("a")) ++*p;        // поиск по StringView без создания String

BTreeMap<int, String> t;              // упорядоченная, есть lower_bound и порядковые операции
StringMap<int> sm;                    // строки на radix-дереве
for (auto& [key, value] : sm.with_prefix("app")) { ... }
```

`BTreeMap` — B+-дерево, в узлах хранятся размеры поддеревьев. `TreapMap` работает с ключами,
которые нельзя копировать. `HashMap` устроен как swiss table: одна аллокация, объект занимает один указатель.

### Ввод-вывод

```cpp
io::println("{:>5} {:.2f}", name, ratio);
String s = format("{} + {} = {}", 1, 2, 3);
```

Строка формата проверяется на этапе компиляции. Для строки, известной только во время работы,
есть `io::runtime_format(s)`. `io::out()` и `io::err()` можно использовать из нескольких потоков,
`io::out().lock()` держит блокировку на несколько записей подряд. Файлы: `io::FileOutput`, `io::FileInput`,
`io::File`; чтение и запись через `Input::read_exact`, `io::copy`, `LimitedInput`, `TeeOutput`.

Числовые обёртки из `yasl/io/fmt.h`: `fmt::num`, `fmt::hex`, `fmt::oct`, `fmt::bin`, `fmt::fixed`,
`fmt::sci`, `fmt::pad`, `fmt::grouped`, `fmt::bytes`, `fmt::percent`.

### Указатели

```cpp
auto p = make_unique<Circle>(1.0);          // Unique<Circle>; можно привести к Unique<Shape>
auto s = make_shared<Config>(...);          // Shared<Config>, счётчик атомарный
Weak<Config> w = s;  w.lock();
```

### Потоки

```cpp
Thread t([] { work(); });                   // деструктор делает join

Guarded<Vector<int>> data;
{ auto g = data.lock(); g->push_back(1); }  // доступ к данным только через guard

auto [tx, rx] = make_channel<int>(8);
tx.send(1);
Optional<int> got = rx.recv();

ThreadPool pool(4);
auto fut = pool.spawn([] { return 42; });
```

Вся синхронизация построена на одном примитиве — ожидании на адресе (`yasl/conc/futex.h`): futex в Linux,
`__ulock_wait` в macOS, `WaitOnAddress` в Windows, на прочих unix — таблица pthread-мьютексов
(`-DYASL_FUTEX_PTHREAD` включает её и в Linux). `Semaphore` — одно 32-битное слово: счётчик и бит «есть
ожидающие». `Mutex` — бинарный семафор. `CondVar`, `RwLock` и `Barrier` — тоже атомарные слова, без pthread.

### Время и случайные числа

```cpp
Instant start = Instant::now();
Duration d = start.elapsed();
io::println("{}", d);                       // например, 2.25s

Random rng = Random::from_os();
rng.range(1, 7);  rng.shuffle(v);
```

### Прочее

* `defer`: `YASL_DEFER { cleanup(); };` или `auto g = scope_exit(f); g.dismiss();`
* `checked_add`, `checked_mul`, `saturating_add`, `narrow<T>(x)` в `yasl/core/math.h`
* ленивые адаптеры: `v | filter(f) | map(g)` и терминальные `collect`, `sum`, `fold`, `find`
* `Variant<A, B>` и `match(v, overloaded{...})`
* файловая система: `Path`, `io::fs::exists`, `io::fs::read_dir` и другие

## Память

Все контейнеры и владеющие типы принимают `mem::Resource*` (по умолчанию `mem::default_resource()`).
Ресурс по умолчанию свой у каждого потока, его можно подменить на время области:

```cpp
mem::Arena arena;
mem::ScopedDefault scope(&arena);
Vector<int> v;                              // берёт память из arena
```

Что есть:

* `Arena`, `InlineArena<N>`: bump-аллокатор, память возвращается целиком (`release`, `reset`);
* `Pool`: списки свободных блоков по размерам от 8 байт до 4 КиБ;
* `ObjPool`: пул объектов, которые уничтожаются вместе с ним, в обратном порядке создания;
* `Tracking`: считает аллокации, текущие и пиковые байты, используется в тестах для поиска утечек;
* `Synchronized<R>`: делает любой ресурс потокобезопасным;
* `Frame<N>`: на время области все контейнеры сначала берут память из буфера на стеке;
* `ThreadLocalPool` и `Pages`: пул мелких блоков на каждый поток и страницы ОС для больших блоков;
  вместе с системным аллокатором они собраны в `chain_resource()`;
* `compose.h`: `StackBuffer`, `Fallback`, `Segregator`, `AlignSplit`, `AsResource` для сборки своих аллокаторов.

`Resource::allocate_at_least()` возвращает реальный размер блока, `Resource::resize()` пробует изменить
блок на месте. `Vector`, `SmallVector` и `String` используют это для типов, которые можно перемещать побайтово.

Под всеми ресурсами лежит `mem::sys`. Бэкенд выбирается при сборке: `-DYASL_MALLOC=libc` (по умолчанию),
`tcmalloc` или `custom`. `tcmalloc` — это [google/tcmalloc](https://github.com/google/tcmalloc), он подменяет
`malloc` и `operator new` во всей программе. Он подключён git-сабмодулем `third_party/tcmalloc` с
`update = none`: обычный `git clone --recursive` его не скачивает. Скрипт сборки сам скачивает сабмодуль
и собирает архив (нужен Bazel или bazelisk):

```sh
third_party/tcmalloc_build/build.sh    # third_party/tcmalloc_build/lib/libtcmalloc_google.a
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DYASL_MALLOC=tcmalloc
```

Версия tcmalloc — коммит сабмодуля. Другой готовый архив можно указать через
`-DYASL_TCMALLOC_LIB=...`. При `cmake --install` архив не устанавливается, его нужно линковать самому
(`meson install` его ставит рядом с `libyasl.a` и прописывает в `yasl.pc`).

На glibc 2.35 и новее glibc сама занимает rseq, и tcmalloc без него работает без per-CPU кешей, медленнее
glibc malloc. Запускайте программу с `GLIBC_TUNABLES=glibc.pthread.rseq=0`. Долгоживущим программам стоит
также крутить `tcmalloc::MallocExtension::ProcessBackgroundActions()` в отдельном потоке: он перераспределяет
per-CPU кеши.

Для `custom` программа сама определяет четыре функции с C-линковкой:
`yasl_sys_alloc`, `yasl_sys_free`, `yasl_sys_resize`, `yasl_sys_good_size` (сигнатуры в `yasl/mem/sys_custom.cpp`).

С контейнерами std ресурсы работают через `yasl/compat`:

```cpp
mem::Pool pool;
std::vector<int, mem::Allocator<int>> v(&pool);
```

## Тесты

Тесты запускаются одним бинарником `yasl_tests`:

```cpp
YASL_TEST(vector_basic) {
  Vector<int> v{1, 2, 3};
  CHECK_EQ(v.size(), size_t(3));
  CHECK_THROWS(v.at(10), OutOfRange);
}
```

Макросы: `CHECK`, `CHECK_EQ`, `CHECK_THROWS`, `CHECK_PANICS`. Утечки проверяются через `mem::Tracking`
(`t.stats().live() == 0`).

В CI (`.github/workflows/ci.yml`) собираются: clang с ASan и UBSan, clang с TSan, GCC в Release с `-Werror`,
сборки с `YASL_MALLOC=tcmalloc` и `custom`, macOS, проверка include-политики и установка с `find_package`.

---

В разработке использовался AI