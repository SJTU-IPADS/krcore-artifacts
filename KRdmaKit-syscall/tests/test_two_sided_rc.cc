#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);
    int cnt = 0;

    const char *addr = "fe80:0000:0000:0000:ec0d:9a03:0078:645e";
    const uint32_t vid = 1024;
    assert( qbind(qd, vid) );
    int ret = qconnect(qd, addr, strlen(addr), vid);
    printf("get qd connect res: %d\n", ret);

    core_req_t req_list[1];
    for (int i = 0; i < 1; ++i) {
        req_list[i] = {
                .addr = 1024,
                .length = 8,
                .lkey = 32,
                .remote_addr=2048,
                .rkey = 32,
                .send_flags = 1,
                .vid = vid,
                .type = Send
        };
    }
    push_core_req_t req = {.req_len=1, .req_list=req_list};
    // push recv
    user_wc_t wc;
    assert(1 == qpush_recv(qd, 16));
    int pop_res = 0;

    for(int i = 0 ;i < 2; ++i){
        // push
        assert(1 == qpush(qd, &req));

        // pop till comp finish
        while (qpop(qd, &wc) != 1 && cnt <= 1000) { ++cnt; }
        printf("client pop success, cnt: %d\n", cnt);
    }


    // pop msg
    cnt = 0;
    while (qpop_msgs(qd, &wc) != 1 && cnt <= 1000) { ++cnt; }
    printf("server pop reply, cnt: %d\n", cnt);
    cnt = 0;
    assert (1 == qpush_recv(qd, 16) ) ;

    // start send back reply
    assert( 1 == qpush(qd, &req) );

    while (qpop(qd, &wc) != 1 && cnt <= 1000 ) { ++cnt; }
    printf("server send back reply, cnt: %d\n", cnt);
    cnt = 0;

    // client get reply
    while (qpop_msgs(qd, &wc) != 1 && cnt <= 1000) { ++cnt; }

    printf("client get send back reply, cnt: %d\n", cnt);
    usleep(200 * 1000);
    return 0;

}

