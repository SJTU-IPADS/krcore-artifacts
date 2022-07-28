# Run Examples

```shell
cd krcore/examples # path to the examples
./run_examples krdmakit_kernel_ud # or krdmakit_kernel_rc
dmesg # see the kernel module output
```

## Reliable Connection (RC)

### Server Side

Server side needs to prepare RDMA context for the server.
```rust
let driver = unsafe { KDriver::create().unwrap() };
let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("server device"))?
        .open_context()
        .map_err(|_| TestError::Error("server context"))?;
```

After the context is prepared, create an RC server.
The Server will listen on the given SERVICE_ID and handle requests from the client side.
```rust
let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
let server_cm = CMServer::new(SERVICE_ID, &rc_server, server_ctx.get_dev_ref())
.map_err(|_| TestError::Error("server cm"))?;
```

After those operations above, the RC server is read to handle RC registration request (also called handshake) and create RC queue pair.
Once the handshake is done, the client can perform RDMA read/write operation.

### Client Side

The client also needs to prepare RDMA context like the server does.
```rust
let driver = unsafe { KDriver::create().unwrap() };
let client_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("server device"))?
        .open_context()
        .map_err(|_| TestError::Error("server context"))?;

```

After the context if prepared, the client needs to create an RC queue pair with the QueuePairBuilder.
```rust
let mut builder = QueuePairBuilder::new(&client_ctx);
builder
    .allow_remote_rw()
    .allow_remote_atomic()
    .set_port_num(client_port);
let client_qp = builder.build_rc().map_err(|_| {
    log::error!("build client rc"); 
    TestError::Error("build client rc")
})?;
```

Yet the `client_qp` is not ready, it has to perform the handshake operation to connect to the server so that it is well-prepared.

The server gid and service id is pre-defined, we can resolve the server's path information from it.

```rust
let gid = server_info.server_gid;
let explorer = Explorer::new(client_ctx.get_dev_ref());
let path = unsafe { explorer.resolve_inner(SERVICE_ID, client_port, gid) }.map_err(|_| {
    log::error!("client resolve path");
    TestError::Error("client resolve path")
})?;
let client_qp = client_qp.handshake(SERVICE_ID, path).map_err(|_| {
    log::error!("client handshake");
    TestError::Error("client handshake")
})?;
```

After handshake done, we get an RC qp ready to perform RDMA operation. For example, we can post an RDMA read to the send queue like below, and do not forget to poll completion queue.
```rust
let _ = client_qp
    .post_send_read(&client_mr, 0..8, false, raddr, rkey)
    .map_err(|_| {
        log::error!("rdma read");
        TestError::Error("rdma read")
    })?;
let _ = client_qp
    .post_send_write(&client_mr, 8..16, true, raddr + 8, rkey)
    .map_err(|_| {
        log::error!("rdma write");
        TestError::Error("rdma write")
})?;
let mut completions = [Default::default(); 5];
let timer = RTimer::new();
loop {
    let ret = client_qp.poll_send_cq(&mut completions).map_err(|_| {
        log::error!("poll client send cq");
        TestError::Error("poll client send cq")
    })?;
    if ret.len() > 0 {
        break;
    }
    if timer.passed_as_msec() > 100.0 {
        log::error!("poll client send cq time out");
        return Err(TestError::Error("poll client send cq time out"));
    }
}
```

For more information, please read to the examples or the unit tests in KRdmaKit.

## Unreliable Datagram (UD)

### Server SIde
Server side needs to prepare for the server context.
```rust
let driver = unsafe { KDriver::create().unwrap() };
let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("server device"))?
        .open_context()
        .map_err(|_| TestError::Error("server context"))?;
```

After the context is prepared, we create a ud server. The ud server listens on SERVICE_ID and handle SIDR request from client side.
```rust
let ud_server = UnreliableDatagramAddressService::create();
let _server_cm = CMServer::new(SERVICE_ID, &ud_server, server_ctx.get_dev_ref())
    .map_err(|_| TestError::Error("cm server"))?;
```

Once the ud server is created, we can build one or more UD queue pairs and register them to the server.
```rust
let mut builder = QueuePairBuilder::new(&server_ctx);
builder.allow_remote_rw().set_port_num(server_port);
let server_qp = builder
    .build_ud()
    .map_err(|_| TestError::Error("build ud qp"))?;
let server_qp = server_qp
    .bring_up_ud()
    .map_err(|_| TestError::Error("query path"))?;
ud_server.reg_ud(QD_HINT, &server_qp);
```

The registered UD qp now can handle work request once generated.
For example, perform `post_recv` on the qp to handle work request to the recv_queue.
Also do not forget to poll completion queue.
```rust
let _ = server_qp
    .post_recv(&server_mr, 0..(TRANSFER_SIZE + GRH_SIZE), server_buf as u64)
    .map_err(|_| TestError::Error("server post recv"))?;
// client post_send after server post_recv
let mut completions = [Default::default(); 10];
let timer = RTimer::new();
loop {
    let res = server_qp
        .poll_recv_cq(&mut completions)
        .map_err(|_| TestError::Error("server recv cq"))?;
    if res.len() > 0 {
        break;
    } else if timer.passed_as_msec() > 40.0 {
        log::error!("time out while poll recv cq");
        break;
    }
}
```

### Client Side

Client also needs to prepare for the context like the server does.

After the client context is prepared, we need to explore the server qp (already been registered in the server side) information, here we call it an endpoint.

```rust
let explorer = Explorer::new(client_ctx.get_dev_ref());
let path = unsafe { explorer.resolve_inner(SERVICE_ID, client_port, gid) }
    .map_err(|_| TestError::Error("resolve path"))?;
let querier = UnreliableDatagramEndpointQuerier::create(&client_ctx, client_port)
    .map_err(|_| TestError::Error("create querier"))?;
let endpoint = querier
    .query(SERVICE_ID, QD_HINT, path)
    .map_err(|_| TestError::Error("query endpoint"))?;
```

Build and bring up a UD queue pair, and we can perform `post_send` to the qp to send data to server.
```rust
let mut builder = QueuePairBuilder::new(&client_ctx);
builder.set_port_num(client_port).allow_remote_rw();
let client_qp = builder.build_ud()
    .map_err(|_| TestError::Error("create client ud"))?;
let client_qp = client_qp.bring_up_ud()
    .map_err(|_| TestError::Error("bring up client ud"))?;

// write value on local memory and send it to server
let client_mr = MemoryRegion::new(client_ctx.clone(), 512)
    .map_err(|_| TestError::Error("create client MR"))?;
let client_buf = client_mr.get_virt_addr() as *mut i8;
unsafe { (*client_buf) = 127 };
let _ = client_qp
    .post_datagram(
        &endpoint,
        &client_mr,
        0..TRANSFER_SIZE,
        client_buf as u64,
        true,
    )
    .map_err(|_| TestError::Error("client post send"))?;

let mut completions = [Default::default(); 10];
let timer = RTimer::new();
loop {
    let ret = client_qp
        .poll_send_cq(&mut completions)
        .map_err(|_| TestError::Error("client poll send cq"))?;
    if ret.len() > 0 {
        break;
    }
    if timer.passed_as_msec() > 15.0 {
        log::error!("time out while poll send cq");
    break;
    }
}
```

For more information, please read to the examples or the unit tests in KRdmaKit.
