#include <pthread.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

int
bd_ibv_exp_query_device(struct ibv_context* context,
                     struct ibv_exp_device_attr* attr);