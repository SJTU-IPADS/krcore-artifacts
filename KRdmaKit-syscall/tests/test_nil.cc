#include <assert.h>
#include <stdio.h>

#include "../../include/syscall.h"

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);

    // issue the sys call
    for (int i = 0; i < 5; ++i) {
        int ret = qnil(qd);
        printf("sanity check nil return: %d\n", ret);
    }

    return 0;
}