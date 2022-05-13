#ifndef LINUX_RPC_RDMA_H
#define LINUX_RPC_RDMA_H

#include_next <linux/sunrpc/rpc_rdma.h>

#ifndef rpcrdma_version

#define RPCRDMA_VERSION                1
#define rpcrdma_version                cpu_to_be32(RPCRDMA_VERSION)

#define rdma_msg       cpu_to_be32(RDMA_MSG)
#define rdma_nomsg     cpu_to_be32(RDMA_NOMSG)
#define rdma_msgp      cpu_to_be32(RDMA_MSGP)
#define rdma_done      cpu_to_be32(RDMA_DONE)
#define rdma_error     cpu_to_be32(RDMA_ERROR)

#endif /* rpcrdma_version */

#endif /* LINUX_RPC_RDMA_H */
