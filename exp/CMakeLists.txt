project(KRdmaKit-motiv-connect)
ADD_DEFINITIONS(-std=c++17)
cmake_minimum_required(VERSION 3.2)
include(deps/deps.cmake)

set(exps
         rc_connect_server
        rc_connect_client
        ibv_fork mem_profile
        rc_connect_client_kernel rc_onesided_client_kernel connect_simulation
        #  one-sided
        krc_client_sync urc_server_sync urc_client_sync krc_client_docker
        #  two-sided
        kernel_twosided_rc_client kernel_twosided_rc_server
        kernel_twosided_rc_zcpy_client kernel_twosided_rc_zcpy_server
        user_twosided_rc_server user_twosided_rc_client
        user_payload_trc_server user_payload_trc_client
        # Control path of connection
        connect_krc_worker connect_krc_trigger
        connect_user_worker connect_user_trigger connect_user_server
        connect_lite_worker connect_lite_trigger
        # Racing-hash
        racing_hash_trigger racing_hash_analyser
        racing_hash_kernel_elastic_worker
        racing_hash_user_elastic_worker racing_hash_user_server
        racing_hash_kernel_elastic_worker_tail_lat racing_hash_user_elastic_worker_tail_lat
        reg_mr

        # RFork of OSDI
        rcopy_client rcopy_server
        # LITE comparison
        lite_rc_client
        )

 add_executable(rc_connect_server motiv-connect/rc_connect_server.cc)
add_executable(rc_connect_client motiv-connect/rc_connect_client.cc)
add_executable(rc_connect_client_kernel motiv-connect/rc_connect_client_kernel.cc)
add_executable(ibv_fork motiv-connect/ibv_fork.cc)
add_executable(mem_profile motiv-connect/mem_profile.cc)
add_executable(connect_simulation motiv-connect/connect_simulation.cc)
add_executable(rc_onesided_client_kernel motiv-connect/rc_onesided_client_kernel.cc)

# one-sided
add_executable(krc_client_sync one-sided/krc_client.cc)
add_executable(krc_client_docker one-sided/krc_client-docker.cc)
add_executable(urc_server_sync one-sided/urc_server.cc)
add_executable(urc_client_sync one-sided/urc_client.cc)

# two-sided
add_executable(kernel_twosided_rc_client two-sided/kernel_twosided_rc_client.cc)
add_executable(kernel_twosided_rc_server two-sided/kernel_twosided_rc_server.cc)
add_executable(kernel_twosided_rc_zcpy_client two-sided/kernel_twosided_rc_zcpy_client.cc)
add_executable(kernel_twosided_rc_zcpy_server two-sided/kernel_twosided_rc_zcpy_server.cc)
add_executable(user_twosided_rc_server two-sided/user_twosided_rc_server.cc)
add_executable(user_twosided_rc_client two-sided/user_twosided_rc_client.cc)
add_executable(user_payload_trc_server two-sided/user_payload_latency_rc_server.cc)
add_executable(user_payload_trc_client two-sided/user_payload_latency_rc_client.cc)

# Control path of connection
add_executable(connect_krc_worker full-mesh/kernel/worker.cc)
add_executable(connect_krc_trigger full-mesh/trigger.cc)
add_executable(connect_user_worker full-mesh/user/worker.cc)
add_executable(connect_user_trigger full-mesh/trigger.cc)
add_executable(connect_user_server full-mesh/user/kv_server.cc)
add_executable(connect_lite_worker full-mesh/kernel/worker.cc)
add_executable(connect_lite_trigger full-mesh/trigger.cc)


# Racing-hash
add_executable(racing_hash_trigger race-hasing/trigger.cc)
add_executable(racing_hash_analyser race-hasing/analyser.cc)
add_executable(racing_hash_kernel_elastic_worker race-hasing/krcore/elastic_worker.cc)
add_executable(racing_hash_kernel_elastic_worker_tail_lat race-hasing/krcore/elastic_worker_tail_lat.cc)
add_executable(racing_hash_user_elastic_worker race-hasing/user/elastic_worker.cc)
add_executable(racing_hash_user_elastic_worker_tail_lat race-hasing/user/elastic_worker_tail_lat.cc)
add_executable(racing_hash_user_server race-hasing/user/server.cc)
add_executable(reg_mr reg_mr.cc)
add_executable(rcopy_server rcopy/server.cc)
add_executable(rcopy_client rcopy/client.cc)

# LITE
add_executable(lite_rc_client LITE/one-sided/lite_client.cc)

# For valgrind
add_compile_options(-g)

foreach(e ${exps})
    target_link_libraries(${e} pthread gflags ibverbs boost_coroutine boost_chrono boost_thread boost_context boost_system r2)
endforeach(e)
