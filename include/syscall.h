#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "./common.h"

static inline int
queue() {
    return open("/dev/krdma", O_WRONLY);
}

// a nil syscall for testing the raw sys performance
static inline int
qnil(int qd) {
    req_t req;
    reply_t reply;

    req.dummy = 73;
    req.reply_buf = &reply;

    if (ioctl(qd, Nil, &req) == -1) {
        return -1;
    }

    return reply.status;
}

/*!
  connect to the remote server
  the addr must be in gid format, e.g.,  fe80:0000:0000:0000:ec0d:9a03:00ca:2f4c
  typically, it can be retrieved via `ibstatus`
 */
static inline int
qconnect(int qd, const char *addr, int addr_sz, int port = 0, int vid = 0) {
    req_connect_t req;
    reply_t reply;
    reply.status = err;

    req.req.dummy = 64;
    req.req.reply_buf = &reply;
    req.conn_req.addr = addr;
    req.conn_req.port = port;
    req.conn_req.addr_sz = addr_sz;
    req.conn_req.vid = vid;

    if (ioctl(qd, Connect, &req) == -1) {
        return -1;
    }

    return reply.status;
}

static inline int
qreg_mr(int qd, unsigned long long address, unsigned int size, unsigned int hint) {
    reg_mr_req_t req;
    reply_t reply;
    req.req.dummy = 64;
    req.reg_mr_req.address = address;
    req.reg_mr_req.size = size;
    req.reg_mr_req.hint = hint;
    req.req.reply_buf = &reply;
    if (ioctl(qd, RegMRs, &req) == -1) {
        return -1;
    }

    return reply.status;
}

static inline int
qpush(int qd, const push_core_req_t *reqq, unsigned char pop_res = 0, unsigned int push_recv_cnt = 0) {
    push_req_t req;
    reply_t reply;
    req.req.reply_buf = &reply;
    req.ext = {.push_recv_cnt = push_recv_cnt, .pop_res = pop_res};

    memcpy(&req.core, reqq, sizeof(push_core_req_t));
    if (ioctl(qd, Push, &req) == -1) {
        return -1;
    }
    return reply.status;
}

// pop an RDMA request from the queue
static inline int
qpop(int qd, pop_reply_t *reply, int vid = 0) {
    pop_req_t req;

    req.req.reply_buf = reply;
    req.pop_vid.vid = vid;

    if (ioctl(qd, Pop, &req) == -1) {
        return -1;
    }
    return reply->header.status;
}

static inline int
qpush_recv(int qd, unsigned int push_count) {
    push_recv_req_t req;
    reply_t reply;
    req.push_recv.push_count = push_count;
    req.req.reply_buf = &reply;

    if (ioctl(qd, PushRecv, &req) == -1) {
        return -1;
    }
    return reply.status;
}

// pop an RDMA request from the queue
static inline int
qpop_msgs(int qd, pop_reply_t *reply, unsigned int pop_count, unsigned int payload_sz = 0) {
    pop_msgs_t req;
    req.pop_count = pop_count;
    req.payload_sz = payload_sz;

    req.req.reply_buf = reply;
    if (ioctl(qd, PopMsgs, &req) == -1) {
        return -1;
    }
    return reply->header.status;
}


static inline int
qbind(int qd, int port) {
    bind_req_t req;
    reply_t reply;
    req.req.dummy = 64;
    req.bind_req.port = port;
    req.req.reply_buf = &reply;
    if (ioctl(qd, Binds, &req) == -1) {
        return -1;
    }

    return reply.status;
}

static inline int
qunbind(int qd, int port) {
    bind_req_t req;
    reply_t reply;
    req.req.dummy = 64;
    req.bind_req.port = port;
    req.req.reply_buf = &reply;
    if (ioctl(qd, UnBinds, &req) == -1) {
        return -1;
    }

    return reply.status;
}

static inline int
qpoll_rpc(int qd) {
    req_t req;
    reply_t reply;

    req.dummy = 73;
    req.reply_buf = &reply;

    if (ioctl(qd, RpcPoll, &req) == -1) {
        return -1;
    }

    return reply.status;
}




