# rust-kernel-rdma-base

> Raw RDMA interface communicate with C linux kernel

This module is the lowest tier to the raw linux kernel,  close to 
the linux native functions/type definitions for `RDMA`. The upper kernel rdma
interface(`KRdmaKit`) could directly use the service in this module. 

## Tested linux kernel version
- kernel `4.15.0-46-generic`
## Rust dependency

install rustc

```sh
curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh
```

Currently, RLib is only tested on a specific version of `rust`: `rustup-nightly` @**version 2020-11-10**.
We will fix problem using the latest rust compiler in the future.

```sh
rustup default nightly-2020-11-10-x86_64-unknown-linux-gnu
rustup component add rust-src
```

## RDMA dependencies
- MLNX_OFED_LINUX-4.4-2.0.7.0 (OFED-4.4-2.0.7)`
