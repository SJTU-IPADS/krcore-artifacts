#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);

    const char *addr = "fe80:0000:0000:0000:ec0d:9a03:0078:645e";
    int ret = qconnect(qd, addr, strlen(addr), 16);
    printf("get qd connect res: %d\n", ret);
    usleep(200 * 1000);
#if 0
    ret = qconnect(qd, addr, strlen(addr), 16);
    printf("second] get qd re-connect res: %d\n", ret);
#endif
    return 0;
}