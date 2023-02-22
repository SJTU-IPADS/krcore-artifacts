## Install 

1. Clone the whole repo and switch to the **develop** branch:

```
git clone https://ipads.se.sjtu.edu.cn:1312/distributed-rdma-serverless/krcore.git --recursive 
git checkout develop
```

2. For the user-space usage, most rust release should work fine. You can check this via:

```
cd /path/to/krcore
cd KRdmaKit # main library
cargo test --features user 
```

To use KRCore at the kernel space, check  [install.md](./install.md) for more detailed steps. 



## Use RDMA w/ KRCore at the user space

Using RDMA is simple. For example, you can check the number of RDMA devices available on your machine with the following code:

```
#[allow(non_snake_case)]

extern crate KRdmaKit;

use KRdmaKit::*;

fn main() {
    println!("Num RDMA devices found: {}", UDriver::create().unwrap().devices().len());
}
```

Be sure to add `KRdmaKit = { path = "path_to_krcore", features = ["user", "rdma-runtime"] }`  to the Cargo.toml. 

---

More examples can be found at `path/to/krcore/KRdmaKit/examples` .

You can run these examples as follows:

```
cargo run --example EXP_NAME --features user
```

Note that for the `ib-exp` example, you need to pass an additional `--features exp` to the above command. 
