#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);
    int ret;
    ret = qbind(qd, 1024);
    printf("first] bind res: %d\n", ret);
#if 1
    ret = qbind(qd, 1024);
    printf("second] bind res: %d\n", ret);
#endif
    ret = qunbind(qd, 1024);
    printf("unbind res: %d\n", ret);

    return 0;
}