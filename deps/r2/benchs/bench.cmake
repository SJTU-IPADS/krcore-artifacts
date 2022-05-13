find_library(boost_serialization_lib NAMES boost_serialization PATHS ./deps/boost PATH_SUFFIXES lib
               NO_DEFAULT_PATH)

set(benchs
bench_server co_client)

add_executable(co_client benchs/co_bench/client.cc)
add_executable(bench_server benchs/bench_server.cc)

foreach(b ${benchs})
  target_link_libraries(${b} pthread gflags ${rocksdb_lib} ${mkl_rt_lib} ${boost_serialization_lib} ${jemalloc_lib} ibverbs boost_coroutine boost_chrono boost_thread boost_context boost_system r2)
  add_dependencies(${b} jemalloc )
endforeach(b)
