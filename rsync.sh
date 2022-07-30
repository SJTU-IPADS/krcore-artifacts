#!/usr/bin/env bash

user="wxd"
#target=("val12" "val14")
target=("val01")

path="./krdmakit"

for machine in ${target[*]}
do
  rsync -i -rtuv \
        $PWD/deps \
        $PWD/KRdmaKit-syscall \
        $PWD/krdmakit-macros \
        $PWD/rdma-shim \
        $PWD/rust-user-rdma \
        $PWD/rust-kernel-rdma \
        $PWD/testlib \
        $PWD/include \
        $PWD/exp  \
        $user@$machine:/home/${user}/${path} \
        --exclude target \
        --exclude Cargo.lock
done
