project(
  'yasl',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.1.0',
  default_options: ['cpp_std=c++20', 'warning_level=3', 'b_ndebug=if-release'],
)

cxx = meson.get_compiler('cpp')
is_windows = host_machine.system() == 'windows'
malloc = get_option('malloc')

yasl_sources = files(
  'yasl/conc/futex.cpp',
  'yasl/conc/mutex.cpp',
  'yasl/conc/ref_count.cpp',
  'yasl/conc/thread.cpp',
  'yasl/core/check.cpp',
  'yasl/core/types.cpp',
  'yasl/io/buffer.cpp',
  'yasl/io/file.cpp',
  'yasl/io/fmt.cpp',
  'yasl/io/fs.cpp',
  'yasl/io/input.cpp',
  'yasl/io/output.cpp',
  'yasl/mem/arena.cpp',
  'yasl/mem/obj_pool.cpp',
  'yasl/mem/page.cpp',
  'yasl/mem/pool.cpp',
  'yasl/mem/resource.cpp',
  'yasl/mem/thread_local_pool.cpp',
  'yasl/os/env.cpp',
  'yasl/os/fd.cpp',
  'yasl/ptr/shared.cpp',
  'yasl/rand/random.cpp',
  'yasl/str/num.cpp',
  'yasl/str/str.cpp',
  'yasl/time/time.cpp',
)

yasl_sources += files('yasl/mem/sys_' + malloc + '.cpp')
if is_windows
  yasl_sources += files('yasl/mem/os_pages_win.cpp')
else
  yasl_sources += files('yasl/mem/os_pages_posix.cpp')
endif

yasl_deps = [dependency('threads')]
if is_windows
  yasl_deps += cxx.find_library('synchronization')
endif
yasl_public_args = []
yasl_private_args = []
yasl_link_args = []
yasl_pc_libs = []

if malloc == 'tcmalloc'
  tcmalloc_lib = get_option('tcmalloc_lib')
  if tcmalloc_lib == ''
    tcmalloc_lib = meson.current_source_dir() / 'third_party/tcmalloc_build/lib/libtcmalloc_google.a'
  endif
  if not import('fs').exists(tcmalloc_lib)
    error('malloc=tcmalloc needs ' + tcmalloc_lib + ': run third_party/tcmalloc_build/build.sh (needs Bazel and the third_party/tcmalloc submodule)')
  endif
  if host_machine.system() == 'darwin'
    yasl_link_args += ['-Wl,-force_load,' + tcmalloc_lib]
  else
    yasl_link_args += ['-Wl,--whole-archive', tcmalloc_lib, '-Wl,--no-whole-archive']
  endif
  yasl_public_args += ['-DYASL_MALLOC_TCMALLOC']
  install_data(tcmalloc_lib, install_dir: get_option('libdir'))
  tcmalloc_installed = '${libdir}/' + import('fs').name(tcmalloc_lib)
  if host_machine.system() == 'darwin'
    yasl_pc_libs = ['-Wl,-force_load,' + tcmalloc_installed]
  else
    yasl_pc_libs = ['-Wl,--whole-archive', tcmalloc_installed, '-Wl,--no-whole-archive']
  endif
endif

if get_option('default_alloc') == 'chain'
  yasl_private_args += ['-DYASL_DEFAULT_ALLOC_CHAIN']
endif

yasl_inc = include_directories('.')

yasl_lib = static_library(
  'yasl',
  yasl_sources,
  include_directories: yasl_inc,
  cpp_args: yasl_public_args + yasl_private_args,
  dependencies: yasl_deps,
  install: true,
)

yasl_dep = declare_dependency(
  link_with: yasl_lib,
  include_directories: yasl_inc,
  compile_args: yasl_public_args,
  link_args: yasl_link_args,
  dependencies: yasl_deps,
)
meson.override_dependency('yasl', yasl_dep)

install_subdir(
  'yasl',
  install_dir: get_option('includedir'),
  exclude_directories: ['test', 'bench'],
)

import('pkgconfig').generate(
  yasl_lib,
  name: 'yasl',
  description: 'Yet Another Standard Library (C++20)',
  extra_cflags: yasl_public_args,
  libraries: yasl_pc_libs,
)

if get_option('tests')
  test_sources = files(
  'yasl/algo/algo_test.cpp',
  'yasl/algo/iter_test.cpp',
  'yasl/compat/compat_test.cpp',
  'yasl/conc/atomic_test.cpp',
  'yasl/conc/channel_test.cpp',
  'yasl/conc/guarded_test.cpp',
  'yasl/conc/semaphore_test.cpp',
  'yasl/conc/thread_pool_test.cpp',
  'yasl/conc/thread_test.cpp',
  'yasl/cont/adapter/adapters_test.cpp',
  'yasl/cont/adapter/priority_queue_test.cpp',
  'yasl/cont/assoc/hash_map_test.cpp',
  'yasl/cont/assoc/map_test.cpp',
  'yasl/cont/assoc/radix_trie_test.cpp',
  'yasl/cont/assoc/string_map_test.cpp',
  'yasl/cont/assoc/trie_test.cpp',
  'yasl/cont/seq/bitset_test.cpp',
  'yasl/cont/seq/deque_test.cpp',
  'yasl/cont/seq/intrusive_list_test.cpp',
  'yasl/cont/seq/list_test.cpp',
  'yasl/cont/seq/small_vector_test.cpp',
  'yasl/cont/seq/vector_test.cpp',
  'yasl/core/check_test.cpp',
  'yasl/core/math_test.cpp',
  'yasl/io/buffer_test.cpp',
  'yasl/io/fmt_test.cpp',
  'yasl/io/fs_test.cpp',
  'yasl/io/io_test.cpp',
  'yasl/mem/mem_test.cpp',
  'yasl/mem/obj_pool_test.cpp',
  'yasl/mem/page_test.cpp',
  'yasl/mem/thread_local_pool_test.cpp',
  'yasl/os/env_test.cpp',
  'yasl/os/fd_test.cpp',
  'yasl/ptr/intrusive_test.cpp',
  'yasl/ptr/shared_test.cpp',
  'yasl/ptr/unique_test.cpp',
  'yasl/rand/random_test.cpp',
  'yasl/str/num_test.cpp',
  'yasl/str/str_test.cpp',
  'yasl/str/symbol_test.cpp',
  'yasl/time/time_test.cpp',
  'yasl/vocab/defer_test.cpp',
  'yasl/vocab/function_test.cpp',
  'yasl/vocab/optional_test.cpp',
  'yasl/vocab/result_test.cpp',
  'yasl/vocab/variant_test.cpp',
  'yasl/test/sys_custom_impl.cpp',
  'yasl/test/test.cpp',
)
  test_args = malloc == 'custom' ? ['-DYASL_MALLOC_CUSTOM'] : []
  yasl_tests = executable(
    'yasl_tests',
    test_sources,
    cpp_args: test_args,
    dependencies: yasl_dep,
    override_options: ['warning_level=2'],
  )
  test('yasl_tests', yasl_tests, timeout: 300)

  cmake_prog = find_program('cmake', required: false)
  if cmake_prog.found()
    root = '-DROOT=' + meson.current_source_dir()
    test(
      'yasl_include_policy',
      cmake_prog,
      args: [root, '-P', meson.current_source_dir() / 'cmake/check_includes.cmake'],
    )
    test(
      'yasl_build_lists',
      cmake_prog,
      args: [root, '-P', meson.current_source_dir() / 'cmake/check_build_lists.cmake'],
    )
    test(
      'yasl_format_compile_fail',
      cmake_prog,
      args: [
        '-DCXX=' + cxx.cmd_array()[0],
        root,
        '-P', meson.current_source_dir() / 'cmake/check_compile_fail.cmake',
      ],
    )
  endif
endif

if get_option('bench')
  absl_deps = []
  foreach m : ['absl_flat_hash_map', 'absl_btree', 'absl_inlined_vector',
               'absl_hash', 'absl_str_format', 'absl_strings']
    absl_deps += dependency(m)
  endforeach
  executable(
    'yasl_bench',
    files(
  'yasl/bench/alloc_bench.cpp',
  'yasl/bench/bench.cpp',
  'yasl/bench/cont_bench.cpp',
  'yasl/bench/str_bench.cpp',
    ),
    dependencies: [yasl_dep] + absl_deps,
  )
endif
