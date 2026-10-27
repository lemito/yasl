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

## Структура

| Модуль | Что внутри |
|---|---|
| `yasl/sys` | type traits и концепты на builtins, `move/forward/swap`, `Optional`, `Result`, `Pair`, `Span`, `Less`, алгоритмы, исключения, хеш, `Mutex` (pthread) |
| `yasl/mem` | `Resource`, `Arena`, `InlineArena<N>`, `Pool`, `Tracking`, `Synchronized<R>` |
| `yasl/vec` | `Vector<T>` |
| `yasl/str` | `StringView`, `String` (copy-on-write), `StringBuilder`, `num` — числа ⇄ текст |
| `yasl/io` | `Output`/`Input`, форматирование `format("{}")`, файлы, строковые потоки, `out()/err()/in()` |
| `yasl/map` | `BTreeMap/BTreeSet`, `TreapMap/TreapSet`, `Trie<V>` |
| `yasl/ptr` | `Arc<T>` |
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
| хранение | массивы элементов в узлах B-дерева | отдельный узел на элемент |
| итераторы | **инвалидируются** при любой вставке/удалении | **стабильны**, пока элемент жив |
| сильные стороны | кэш-локальность, мало памяти | `kth`, `rank`, `index_of`, `split`, `join` за O(log n) |

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

### Строки

```cpp
String a = "shared";
String b = a;              // O(1), один буфер
b.set(0, 'S');             // копирование только сейчас; изменяемого operator[] нет намеренно

for (StringView part : "a,b,,c"_sv.split(',')) { ... }
auto kv = "k = v"_sv.split_once('=');      // Optional<Pair<StringView, StringView>>
Optional<int> n = "42"_sv.parse<int>();

StringBuilder sb;          // это io::Output, пишет прямо в буфер String
sb << "id=" << 42 << ' ' << 0.1;
sb.format(" [{:>6.2f}]", 3.14159);
String s = sb.take();      // без копирования
String t = format("{}-{:03}", "x", 7);
```

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
типы `d x X o b c f e s`. Свои типы — через `operator<<(io::Output&, const T&)`
или `format_value(io::Output&, const T&, const io::FormatSpec&)` (находится по ADL).

## Потокобезопасность

`String` и `Arc` используют атомарный счётчик ссылок; копии можно свободно передавать между потоками.
Контейнеры, потоки и `Arena`/`Pool`/`Tracking` — однопоточные; для общего ресурса есть `Synchronized<R>`.

## Тесты

Свой мини-фреймворк в `yasl/test` (сам написан на yasl). В тестах std используется только как эталон
для сравнения (`std::map`, `to_chars`/`from_chars`) и в тестах совместимости.
Проверено на clang 18 (ASan+UBSan, TSan) и GCC 15 (Release, `-Werror`).
