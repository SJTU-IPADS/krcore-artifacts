#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);

    const char *addr = "fe80:0000:0000:0000:ec0d:9a03:0078:645e";
//    int ret = qconnect(qd, 16, addr, strlen(addr));
    int ret = qconnect(qd, addr, strlen(addr), 16);
    printf("get qd connect res: %d\n", ret);

    core_req_t req_list[1];
    for (int i = 0; i < 1; ++i) {
        req_list[i] = {
                .addr = 1024,
                .length = 1024,
                .lkey = 32,
                .remote_addr=2048,
                .rkey = 32,
                .send_flags = 1,
                .vid = 16,
                .type = Read,
        };
    }

    push_core_req_t req = {.req_len=1, .req_list=req_list};
    int push_res = qpush(qd, &req);
    printf("push res: %d\n", push_res);
    pop_reply_t reply;

    int pop_res = 0;
    int cnt = 0;
    // pop till comp finish
    while ((pop_res = qpop(qd, &reply)) != 1) { ++cnt; }
    printf("[%d] pop_res: %d, pop_count: %d\n", cnt, pop_res, reply.pop_count);

    for(int i= 0 ;i < reply.pop_count ;++i) {
        user_wc_t wc = reply.wc[i];
        printf("At [%d]  pop wc.status: %d, wc.wr_id: %llu, wc.imm_data: %lld\n",
               i, wc.wc_status, wc.wc_wr_id, wc.imm_data);
    }
    usleep(200 * 1000);
    return 0;

}

