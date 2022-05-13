#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);

    // issue the sys call
    for (int i = 0; i < 1000 * 20; ++i) {
        int ret = qpoll_rpc(qd);
        usleep(5000);
    }

    return 0;
}