#include <assert.h>
#include <stdio.h>
#include <vector>
#include <mm_malloc.h>
#include "../../include/syscall.h"

#define K 1024
#define M 1024 * K

int
main(int argc, char *argv[]) {
    int qd = queue();
    assert(qd >= 0);
    std::vector<int> vec = {
//            128 * K
//            128, 512, 1 * K, 4 * K, 16 * K, 256 * K, 512 * K, 1 * M, 4 * M,
//            16 * M,
//            64 * M,
//            128 * M
            512 * M
    };

    for (auto size: vec) {
        void *ptr = malloc(size);
        int ret = 0;
        ret = qreg_mr(qd, (uint64_t) ptr, size, 16);
    }

    return 0;
}