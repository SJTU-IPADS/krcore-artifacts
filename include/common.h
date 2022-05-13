#ifndef RLIB_SYS_H
#define RLIB_SYS_H
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
    RegMRs,
    RpcPoll,
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
    unsigned long long opaque;
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
    unsigned long long addr;
    unsigned int length;
    unsigned int lkey;
    // remote params
    unsigned long long remote_addr;
    unsigned int rkey;
    unsigned int send_flags;

    unsigned int vid;           // extended for twosided
    enum lib_r_req type; // RDMA request type
} core_req_t;


typedef struct {
    unsigned int req_len;            // length of request element
    core_req_t *req_list;   // first request element address
} push_core_req_t;

typedef struct {
    unsigned int push_recv_cnt; // check push recv at first
    unsigned char pop_res;
    unsigned char rc;
} push_ext_req_t ;

typedef struct {
    req_t req;
    push_core_req_t core;
    push_ext_req_t ext;
} push_req_t;
/* Push end */

/* Pop */
typedef struct {
    unsigned int wc_op;     // ib_wc_op, should match the request QP
    unsigned int wc_status; // ib_wc_ok, etc
    unsigned int imm_data;
    unsigned long long wc_wr_id;
} user_wc_t;

enum wc_consts {
    pop_wc_len = 2048,
};

typedef struct {
    reply_t header;
    user_wc_t wc[pop_wc_len];
    unsigned int pop_count;
} pop_reply_t;


typedef struct {
    int port;              // virtual identifier
} bind_t;

typedef struct {
    req_t  req;
    bind_t bind_req;
} bind_req_t;

typedef struct {
    unsigned long long address;
    unsigned int size;  // size of the MR
    unsigned int hint;  // mr hint
} reg_mr_t;

typedef struct {
    req_t  req;
    reg_mr_t reg_mr_req;
} reg_mr_req_t;

typedef struct {
    int vid;
} pop_t;

typedef struct {
    req_t req;
    pop_t pop_vid;
} pop_req_t ;

typedef struct {
    req_t req;
    unsigned int pop_count;
    unsigned int payload_sz;
} pop_msgs_t ;

typedef struct {
    int push_count;
} push_recv_t;

typedef struct {
    req_t req;
    push_recv_t push_recv;
} push_recv_req_t;
#endif