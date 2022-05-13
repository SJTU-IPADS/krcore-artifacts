project(KRdmaKit-syscall-tests)
ADD_DEFINITIONS(-std=c++17)
cmake_minimum_required(VERSION 3.2)

set(tests
        test_nil test_connect test_rc
        test_bind test_poll_rpc
        test_reg_mr
        )

add_executable(test_nil test_nil.cc)
add_executable(test_connect test_connect.cc)
add_executable(test_rc test_rc.cc)
add_executable(test_bind test_bind.cc)
add_executable(test_poll_rpc test_poll_rpc.cc)
add_executable(test_reg_mr test_reg_mr.cc)

