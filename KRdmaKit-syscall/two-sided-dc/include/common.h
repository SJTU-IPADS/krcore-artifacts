#ifndef RLIB_SYS_H
#define RLIB_SYS_H
#include <stdint.h>

enum lib_r_req {
    Read = 0,
    Write,
    Send,
    SendImm,
    WriteImm,
};

enum lib_r_cmd {
    // the syscall does nothing, just returns
    Nil = 0,

    // connect to the remote
    Connect = 1,
    RegMR,
    Push,
    PushRecv,
    Pop,
    PopMsgs,
    Binds,
    UnBinds,
};

enum reply_status {
    err = 0,
    ok = 1,
    timeout,
    already_connected,
    already_bind,
    addr_error,
    nil, // e.g., when the QP's cq is empty
    not_connected,
    not_bind,
};

typedef struct {
    int dummy;
    void *reply_buf;
} req_t;

typedef struct {
    int status;
    uint64_t opaque;
} reply_t;

/* Connect */
typedef struct {
    const char *addr;       // target addr
    int port;                // port
    int addr_sz;            // addr string length
    int vid;              // virtual identifier
    int mr_handler;          // mr index handler
    int length;         // message length
} connect_t;

typedef struct {
    req_t req;
    connect_t conn_req;
} req_connect_t;
/* Connect end */


/* Push */
typedef struct {
    // local sge params
    uint64_t addr;
    uint32_t length;
    uint32_t lkey;
    // remote params
    uint64_t remote_addr;
    uint32_t rkey;
    uint32_t send_flags;

    uint32_t vid;           // extended for twosided
    enum lib_r_req type; // RDMA request type
} core_req_t;


typedef struct {
    uint32_t req_len;            // length of request element
    core_req_t *req_list;   // first request element address
} push_core_req_t;

typedef struct {
    uint32_t push_recv_cnt; // check push recv at first
    uint8_t pop_res;
} push_ext_req_t ;

typedef struct {
    req_t req;
    push_core_req_t core;
    push_ext_req_t ext;
} push_req_t;
/* Push end */

/* Pop */
typedef struct {
    uint32_t wc_op;     // ib_wc_op, should match the request QP
    uint32_t wc_status; // ib_wc_ok, etc
    uint32_t imm_data;
    uint64_t wc_wr_id;
} user_wc_t;

enum wc_consts {
    pop_wc_len = 2048,
};

typedef struct {
    reply_t header;
    user_wc_t wc[pop_wc_len];
    uint32_t pop_count;
} pop_reply_t;


typedef struct {
    int port;              // virtual identifier
} bind_t;

typedef struct {
    req_t  req;
    bind_t bind_req;
} bind_req_t;

typedef struct {
    int vid;
} pop_t;

typedef struct {
    req_t req;
    pop_t pop_vid;
} pop_req_t ;

typedef struct {
    req_t req;
    uint32_t pop_count;
    uint32_t payload_sz;
} pop_msgs_t ;

typedef struct {
    int push_count;
} push_recv_t;

typedef struct {
    req_t req;
    push_recv_t push_recv;
} push_recv_req_t;
#endif