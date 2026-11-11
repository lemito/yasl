# Архитектурные контракты yasl

Этот документ фиксирует границы слоёв и правила владения, которые проверяются
тестами и include-политикой. Новая зависимость между слоями требует обновления
таблицы и проверки `cmake/check_layer_boundaries.cmake`.

## Слои и зависимости

| Слой | Назначение | Разрешённые зависимости | Платформенные исключения |
|---|---|---|---|
| `core` | типы, traits, память байтов, проверки, арифметика | только `core` и разрешённые C/OS headers | `execinfo` в panic backend |
| `vocab` | `Optional`, `Result`, `Variant`, функции | `core` | нет |
| `mem` | `Resource`, арены, pools, страницы | `core`, `conc` только для синхронизированных ресурсов | `mmap`/Windows virtual memory |
| `str` | views, строки, парсинг, builder | `core`, `mem`, `vocab` | нет |
| `cont` | последовательные и ассоциативные контейнеры | `core`, `mem`, `str` для string containers | нет |
| `time` | duration и часы | `core` | `clock_gettime` на POSIX |
| `io` | форматирование, файлы, input/output | `core`, `mem`, `str`, `time` | POSIX/Windows file APIs |
| `os` | fd и environment | `core`, `mem`, `str`, `vocab` | platform backend для fd/env |
| `conc` | потоки и синхронизация | `core`, `mem`, `time`, `vocab` | pthread/futex или Windows primitives |
| `ptr` | owning и intrusive pointers | `core`, `mem`, `conc` для atomic refcount | atomic implementation |

`core/math_raw.h` остаётся независимым от `vocab`; `core/math.h` предоставляет
удобные `Optional`-обёртки. `time/time.h` содержит только представление времени и
арифметику; форматирование подключается через `io/output.h`.

## Lifetime и views

| Тип | Владение | Срок действия представления |
|---|---|---|
| `StringView` | не владеет байтами | до изменения/уничтожения исходной строки или среды |
| `Span<T>` | не владеет элементами | до изменения ёмкости или уничтожения контейнера |
| `io::MemMap` | владеет OS mapping | до деструктора или move-присваивания; `flush()` проверяет ошибки записи |
| `mem::Arena` | владеет всеми блоками арены | объекты и views живут до `reset`/деструктора арены |
| `mem::Frame` | владеет вложенными frame allocations | до выхода из frame scope; escaped pointers запрещены проверкой |
| `mem::ObjPool` | владеет созданными объектами | до `reset`/деструктора pool; уничтожение в обратном порядке |
| `StringBuilder::view()` | view внутреннего буфера | до следующей записи, `reserve`, `take` или уничтожения builder |

`String` владеет копируемым COW-буфером. `MutStr` владеет уникальным буфером и
никогда не возвращает view, переживающий изменение ёмкости.

## Матрица операций владения

| Тип | Copy | Move | Copy assignment | Move assignment | Исключительная гарантия |
|---|---|---|---|---|---|
| `Vector`/`Deque`/`SmallVector` | глубокая копия | перенос буфера | transactional | transactional | strong для роста |
| `List`/деревья | глубокая копия | перенос корня | transactional | transactional | strong для вставки |
| `String` | COW snapshot | перенос буфера | COW snapshot | перенос | strong при аллокации |
| `MutStr` | глубокая копия | перенос | transactional | transactional | strong при аллокации |
| `Unique` | запрещено | перенос | запрещено | освобождение старого ресурса | базовая при destroy |
| `Shared`/`Weak` | atomic retain | перенос handle | retain/release | release старого handle | `noexcept` для handle |
| `IntrusivePtr` | retain объекта | перенос | retain/release | release старого | зависит от counter |
| `Optional`/`Result`/`Variant` | если значение копируемо | если значение перемещаемо | transactional | transactional | strong при конструировании |

`mem::Resource::allocate_at_least` возвращает фактический размер блока не меньше
запрошенного; последующий `resize` сохраняет данные до `min(old,new)`, соблюдает
alignment и передаёт фактический размер в `deallocate`. Пользовательские ресурсы
обязаны принимать только указатель и параметры, которые они выдали сами.

Ошибки системных вызовов представлены `io::Error` там, где API возвращает
`Result`; ошибки конструирования и нарушения контрактов остаются исключениями и
проверками `YASL_ASSERT`/`YASL_VERIFY`.
