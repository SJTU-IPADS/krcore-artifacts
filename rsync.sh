#!/usr/bin/env bash

user="lfm"
target=("val12" "val14")
path="./krdmakit"

for machine in ${target[*]}
do
  rsync -i -rtuv \
        $PWD/deps \
        $PWD/KRdmaKit-syscall \
        $PWD/krdmakit-macros \
        $PWD/rust-kernel-rdma \
        $PWD/testlib \
        $PWD/include \
        $PWD/exp \
        $user@$machine:/home/${user}/${path} \

done
