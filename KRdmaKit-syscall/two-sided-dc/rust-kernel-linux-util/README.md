# rust-kernel-linux-util
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
