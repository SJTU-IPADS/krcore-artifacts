#!/usr/bin/env bash

target="wxd@val01:~/projects/rust-kernel-module"

rsync -i -rtuv \
      $PWD/../. \
      $target \
      --exclude '../target' \
