project(KRdmaKit-syscall-tests)
ADD_DEFINITIONS(-std=c++17)
cmake_minimum_required(VERSION 3.2)

set(tests
        test_nil test_connect test_rc
        test_bind
        )

add_executable(test_nil test_nil.cc)
add_executable(test_connect test_connect.cc)
add_executable(test_rc test_rc.cc)
add_executable(test_bind test_bind.cc)

