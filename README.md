# yasl
Yet Another Standard Library For C++

Своя стандартная библиотека для C++20. Ядро **не использует стандартную библиотеку C++**:
только то, без чего язык не работает, и то, что даёт ОС.

| Разрешено в ядре | Зачем |
|---|---|
| `<new>`, `<initializer_list>`, `<compare>`, `<cstddef>`, `<cstdint>` | языковая поддержка: placement new, `{...}`, типы результата `<=>` |
| `<atomic>` | атомики |
| `stdlib.h`, `string.h`, `unistd.h`, `fcntl.h`, `sys/stat.h`, `errno.h`, `pthread.h` | ОС / libc: `malloc`, `open/read/write`, `strerror`, мьютекс |

Правило проверяется тестом `yasl_include_policy` (`cmake/check_includes.cmake`): любой другой
`#include <...>` в ядре ломает сборку тестов. Собранная `libyasl.a` ссылается только на libc/POSIX
и ABI-рантайм C++ (исключения, RTTI) — ни одного символа `std::`.

Совместимость со std вынесена в `yasl/compat/` и подключается только явно.

## Сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DYASL_SANITIZE=ON
cmake --build build -j
cd build && ctest                # тесты + проверка include-политики
./build/yasl_tests btree         # только тесты, в имени которых есть "btree"
```

Установка и использование из другого проекта:

```sh
cmake --install build --prefix /opt/yasl
# CMakeLists.txt потребителя:
#   find_package(yasl 0.1 REQUIRED)
#   target_link_libraries(app PRIVATE yasl::yasl)
```

## Структура

| Модуль | Что внутри |
|---|---|
| `yasl/sys` | type traits и концепты на builtins, `move/forward/swap`, `Optional`, `Result`, `Function`, `Variant`+`match`, `defer`, `checked_*`, `sort`, `Duration`/`Instant`/`SystemTime`, `Random` (xoshiro256**), `env::var`/`env::args`, `Thread`, `Atomic<T>`, `WaitGroup`/`Latch`/`ThreadPool`/`Future`, `Guarded<T>`/`RwLock<T>`/`CondVar`/`Once`/`OnceLock`/`Lazy`, `Channel` (`make_channel`), `Pair`, `Span`, `Less`, алгоритмы, исключения, хеш, `Mutex` (pthread) |
| `yasl/mem` | `Resource`, `Arena`, `InlineArena<N>`, `Pool`, `Tracking`, `Synchronized<R>` |
| `yasl/vec` | `Deque<T>`, `SmallVector<T, N>`, `PriorityQueue<T, Cmp>`, `BitSet`, `Vector<T>` (+ `retain`, `dedup`, `sort`, `sort_by_key`, `contains`, `index_of`, `first`/`last`, `extend`, `drain`, `split_off`, `chunks`/`windows`) |
| `yasl/str` | `StringView`, `String` (copy-on-write), `StringBuilder`, `num` — числа ⇄ текст |
| `yasl/io` | `Output`/`Input`, форматирование `format("{}")`, файлы, строковые потоки, `out()/err()/in()` |
| `yasl/map` | `HashMap/HashSet` (`hash_map.h`), `BTreeMap/BTreeSet`, `TreapMap/TreapSet`, `Trie<V>` |
| `yasl/ptr` | `Unique<T>`, `Shared<T>` + `Weak<T>` (`Arc` — устаревший алиас) |
| `yasl/compat` | мост к std: `mem::Allocator<T>`, pmr-адаптеры, `std::hash`, `std::formatter`, `std::ostream` |

## Память

`mem::Resource` — базовый класс аллокаторов: `allocate_at_least()` (реальный размер блока),
`create<T>()`/`destroy()`, поточный ресурс по умолчанию (`mem::ScopedDefault scope(&arena);`).

* `Arena` — bump-аллокатор чанками, `release()`/`reset()`, откат последней аллокации;
* `InlineArena<N>` — арена с буфером внутри объекта;
* `Pool` — size-классы 8 Б … 4 КиБ со списками свободных блоков;
* `Tracking` — счётчики аллокаций/байтов/пика;
* `Synchronized<R>` — потокобезопасная обёртка.

### С обычными std-контейнерами — через `yasl/compat`

```cpp
#include <yasl/compat/allocator.h>
#include <yasl/compat/pmr.h>

mem::Pool pool;
std::vector<int, mem::Allocator<int>> v(&pool);
mem::PmrAdapter pmr(&pool);
std::pmr::vector<int> pv(&pmr);
```

## Контейнеры

Принимают `mem::Resource*` значением (по умолчанию `mem::default_resource()`).
Имена методов как в std (`push_back`, `begin`, `size`…), итераторы совместимы с std-алгоритмами.

### Два Map

| | `BTreeMap` / `BTreeSet` (= `Map` / `Set`) | `TreapMap` / `TreapSet` |
|---|---|---|
| хранение | B+-дерево: элементы в связанных листьях | отдельный узел на элемент |
| итераторы | **инвалидируются** при любой вставке/удалении | **стабильны**, пока элемент жив |
| сильные стороны | кэш-локальность, мало памяти, обход за O(n), `from_sorted`, `range(lo, hi)` | `split`, `join` за O(log n) |
| `kth`, `rank`, `index_of` | O(log n) | O(log n) |
| ключи | копируемые (разделители во внутренних узлах); значения могут быть move-only | любые, в т.ч. move-only |

`erase(it)` у обоих возвращает итератор на следующий элемент. Общий API: `operator[]`, `get()` → указатель
или `nullptr`, `get_or`, `at`, `insert`, `insert_or_assign`, `try_emplace`, `lower_bound`/`upper_bound`,
гетерогенный поиск (`map.get("key"_sv)` без временной `String`), элементы с полями `key`/`value`.

```cpp
Map<String, int> m;
m["apple"] = 3;
for (auto& [k, v] : m) { ... }

TreapMap<int, int> t;
t.kth(10); t.rank(42);
TreapMap<int, int> tail; t.split(100, tail);
```

### HashMap / HashSet

`#include <yasl/map/hash_map.h>`. Открытая адресация в стиле Swiss table (control-байты, группы по 8 через SWAR,
максимальная загрузка 7/8), одна аллокация на таблицу, хеш смешивается со случайным seed процесса (HashDoS).
Порядка нет, итераторы инвалидируются при rehash. API как у `MapOps`/`SetOps` плюс `reserve`, `load_factor`,
`erase_if(pred)`, `retain(pred)`; поиск гетерогенный: `m.get("k"_sv)`.

```cpp
HashMap<String, int> counts;
counts["x"] += 1;
counts.retain([](auto& e) { return e.value > 1; });
```

### Строки

```cpp
String a = "shared";
String b = a;              // O(1), один буфер
b.set(0, 'S');             // копирование только сейчас; изменяемого operator[] нет намеренно

for (StringView part : "a,b,,c"_sv.split(',')) { ... }
auto kv = "k = v"_sv.split_once('=');      // Optional<Pair<StringView, StringView>>
Optional<int> n = "42"_sv.parse<int>();
Result<int, ParseError> r = "4x"_sv.try_parse<int>();   // r.error().kind() == ParseErrorKind::InvalidDigit

for (StringView line : text.lines()) { ... }            // без пустого хвоста, \r\n -> \n
for (StringView w : text.split_whitespace()) { ... }
for (char32_t c : text.chars()) { ... }                   // UTF-8; char_count(), is_valid_utf8(), StringBuilder::push_char(cp)
"pre-x"_sv.strip_prefix("pre-");                          // Optional<StringView>
String u = to_upper("abc");  replace_all(s, "a", "b");  repeat("ab", 3);  join(", ", names);

StringBuilder sb;          // это io::Output, пишет прямо в буфер String
sb << "id=" << 42 << ' ' << 0.1;
sb.format(" [{:>6.2f}]", 3.14159);
String s = sb.take();      // без копирования
String t = format("{}-{:03}", "x", 7);
```

### Ленивые адаптеры (`yasl/sys/iter.h`)

```cpp
using namespace yasl::iter;
auto r = v | filter([](int x) { return x % 2 == 0; }) | map([](int x) { return x * x; }) | collect<Vector>();
int total = range(1, 101) | sum();
for (auto [i, x] : v | enumerate()) ...
```

`map filter take skip enumerate zip reverse range` и терминальные `collect<C> sum count fold any all find`.
Источник — lvalue-контейнер (временный контейнер отвергается при компиляции).

### Числа (`yasl/str/num.h`)

Свои алгоритмы вместо `printf`/`strtod`/`to_chars`:
* вывод double/float кратчайшим round-trip представлением (Dragon4 на длинной арифметике): `0.1`, `1e+100`;
* fixed / scientific с заданной точностью и корректным округлением;
* корректно округлённый разбор (точное деление больших целых + быстрый путь Клингера).

Тесты сверяют результаты со стандартной библиотекой на десятках тысяч случайных значений.

## Ввод-вывод (`yasl/io`)

`io::Output` и `io::Input` — буферизованные потоки: быстрый путь (memcpy в окно буфера) в базовом классе,
производные классы решают, куда идут байты.

| Вывод | Ввод |
|---|---|
| `FileOutput` (POSIX `write`) | `FileInput` (POSIX `read`) |
| `StringBuilder` (в `String`) | `StringInput` (из памяти, без копирования) |
| `ArrayOutput` (в фиксированный буфер) | |
| `NullOutput` (считает байты) | |
| `io::out()`, `io::err()` | `io::in()` (перед чтением сбрасывает `out()`) |

```cpp
io::out() << "x = " << 42 << io::endl;
io::println("{:<10}|{:>8.3f}", name, value);

io::FileOutput file("data.txt");
file.format("{} {}\n", 1, 2);

io::FileInput in("data.txt");
int a, b;
if (in >> a >> b) ...
for (String line; in.read_line(line);) ...

String all = io::read_file("data.txt");
```

Форматирование: `{}`, `{0}`, `{:[[fill]align][+][#][0][width][.precision][type]}`,
типы `d x X o b c f e s`. Строка-литерал проверяется **при компиляции** (скобки, число аргументов, индексы,
буква типа для типа аргумента); строку, известную только в рантайме, передавайте через `io::runtime_format(s)`
(ошибка тогда — `FormatError`). Свои типы — через `operator<<(io::Output&, const T&)`
или `format_value(io::Output&, const T&, const io::FormatSpec&)` (находится по ADL).

Числовые обёртки `yasl/io/fmt.h` (работают в потоках и внутри `format("{}")`):

```cpp
sb << "t=" << fmt::fixed(dt, 3) << " id=" << fmt::hex(id).width(8).zero_pad();
out << fmt::grouped(1000000) << ' ' << fmt::bytes(1536) << ' ' << fmt::percent(0.256, 1);
// 1_000_000 1.5 KiB 25.6%
```

`fmt::num hex HEX oct bin fixed sci pad grouped bytes percent`; модификаторы `.width .fill .left .right .center .plus .alt .zero_pad .precision`.

## Потокобезопасность

`String` и `Shared` используют атомарный счётчик ссылок; копии можно свободно передавать между потоками.
Контейнеры, потоки и `Arena`/`Pool`/`Tracking` — однопоточные; для общего ресурса есть `Synchronized<R>`.

`io::out()` / `io::err()` — `io::SharedOutput`: `format`, `print`, `println`, `eprintln` собирают текст целиком и пишут
его одним куском под мьютексом, строки разных потоков не перемешиваются. `operator<<` и `write` **не**
синхронизированы; чтобы несколько записей шли подряд, возьмите `io::Locked out = io::out().lock();`.

## Тесты

Свой мини-фреймворк в `yasl/test` (сам написан на yasl). В тестах std используется только как эталон
для сравнения (`std::map`, `to_chars`/`from_chars`) и в тестах совместимости.
Проверено на clang 18 (ASan+UBSan, TSan) и GCC 15 (Release, `-Werror`).
