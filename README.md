# KRCore: rust-based communication library for using RDMA 

KRCore has two parts. The first is a rust-based library for using RDMA, 
i.e., by adding high-level abstractions to low-level RDMA libraries (ibverbs). 
The second is a kernel-space component to provide fast user-space RDMA-capable connection setup,
which builds on the first part. 

## Rust-based RDMA library 

The goals of this part is:
1. Simplify building RDMA-based applications with rust
2. Using rust to manage all the (local) lifecycles of RDMA resources (e.g., QP) in a safe and transparent way
3. A unified codebase to run applications both in the kernel-space and user-space. 

We have built basic support for the first and the second cases. 
We are currently refining the code to eliminate non-rust style code as much as possible. 

We are further developing support for user-space.

## Kernel-space support for fast RDMA connections

The second is a kernel-space component to provide fast user-space RDMA-capable connection setup.  For details, please refer to our USENIX ATC 2022 paper: https://www.usenix.org/conference/atc22/presentation/wei

Please refer to [install.md](docs/install.md) for the environment setup and [exp.md](docs/exp.md) for the evaluations. All documents can be found in the `doc` directory of the artifact.

Since KRCore is a kernel-space solution with RDMA, we should first build KRCore from source at the machines involved in the evaluations. Afterward, we provide scripts so that all the evaluations can be done on a single controller machine. 

## Getting Started Instructions

**Overview of roles of machines involved in the evaluations**

> At high-level, we need one machine to be KRCore **server** (that serves RDMA control path and data path operations), a varied number of machines to be KRCore **clients** (that issues RDMA control path and data path operations to the server),  and one **controller** machine that conduct experiments by issuing commands to the clients and server. The server and clients should connect with RDMA. The controller can be any machine that can establish ssh connections to the clients and server. 
>
> Except for building KRCore dependncies at clients and servers, all AE can be finished via scripts at the controller. 

To get started, please first select a controller machine (e.g., your laptop or a PC in the lab). 

**At the controller**. clone the repo to some directory (`/path/to/krcore`)  and export it:

```sh
git clone https://github.com/SJTU-IPADS/krcore-artifacts.git
export PROJECT_PATH=/path/to/krcore
```

**At the controller**.  Then fetch the third-party dependencies.

```sh
cd ${PROJECT_PATH}
sh git_init.sh
```

**At the controller**.  Then synchronize all the files to **all the hosts*** of clients and server. We have prepread a sync script (`rsync.sh`)  shown below that you can use for the synchronization. 

```sh
user="your_user_name"
target=("host1" "host2") ## all the client and server hosts 
path="./krdmakit"

for machine in ${target[*]}
do
  rsync -i -rtuv \
        $PWD/deps \
        $PWD/KRdmaKit-syscall \
        $PWD/rust-kernel-rdma \
        $PWD/krdmakit-macros  \        
        $PWD/testlib \
        $PWD/include \
        $PWD/mlnx-ofed-4.9-driver \
        $PWD/exp \
        $user@$machine:/home/${user}/${path} \

done
```

**Important!!!** Please ensure each host of the clients and server has the same  login username and password  (i.e. user="test_user",  pwd="my_pwd" for each host). Also, the path of the KRCore base is the same at all of them. For example, in the above `rsync.sh`, all the KRCore code are synchronized to `/home/${USER}/projects/krdmakit` at the clients and server. 

Afterwards, please go to the next section for installing KRCore on a specific host. 

### Build & Install KRCore on the server and clients

See [install.md](docs/install.md) (in `${PROJECT_PATH}/doc`)for installing KRCore on a **single** machine. All the hosts involved should follow the same steps. 

After finish the install instructions in [install.md](docs/install.md) on all the involved servers, we can now start to reproduce results of KRCore shown below. 



## kick-the-tires

If **Check if the configuration is correct** in **Run Experiments** of [exp.md](docs/exp.md) passes, then everything will be fine. Please refer to [exp.md](docs/exp.md)  for more details. 



## Key results and claims  

There are three key results of the evaluations of the paper:

1. KRCore can significantly improve the performance of RDMA control path (compared with verbs), e.g., 1000X+ higher throughput. 

   Corresponding paper figures: Figure 8a 

2. KRCore have a comparable RDMA data path performance (compared with verbs) for common RDMA operations, e.g., one-sided RDMA and two-sided RDMA. 

   Corresponding paper figures: Figure 10-11

3. KRCore can improve the application (RaceHashing) bootstrap time when starting and connecting new nodes for the computation. 

   Corresponding paper figures: Figure 14

We focus on the reproducing of these figures---others are similar in principle (or is less relevant to the key results), but is challenging for automatic reproduction due to configuration issues. For how to reproduce the key result figures, please refer to the next section. 

## Instructions for re-producing results

See [exp.md](docs/exp.md) for more information. (in `${PROJECT_PATH}`)

