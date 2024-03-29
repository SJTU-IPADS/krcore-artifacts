## Software Installation

This document shows all of the essential software installation process on **servers** or **clients**. We assume the steps are executed at the **client** or **server** by default throughout this README. 

Software preparation list: 

- OS: Ubuntu16.04 or 18.04  (**bare-metal OS**, not VM or containers for kernel-space)
- Linux kernel: 4.15.0-46-generic
- MLNX_OFED driver: 4.9-3.1.5.0
- Rustc: nightly-2022-02-04-x86_64 and nightly-2022-02-04-aarch64 (only tested for user-space)
- Clang-9

> If you have trouble configuring the OS, we can provide a few number of servers with the experiment environment.  

Pre-assumptions:

- Please set your project path on servers by `export PROJECT_PATH=/your/path`
- Please ensure you have the `sudo` permissions (to install & remove kernel modules and configure Linux kernels). 

[toc]

## 1. Configure the linux kernel to use

**This is optional if you are using the user-space KRCore**. 

If you are using machines provided by us, then no need for this step.  Otherwise, follow the below steps for the configurations (assuming we are using Ubuntu): 

```sh
sudo apt install linux-image-4.15.0-46-generic \
		linux-headers-4.15.0-46-generic -y
sudo update-grub

# change the GRUB_DEFAULT in /etc/default/grub as 
## 	GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 4.15.0-46-generic"
sudo vi /etc/default/grub


# Then reboot the machine
sudo reboot -n
```

After reboot the machine, check it out by `uname -r` to see whether the kernel version has been set correctly. A correct output should be: 

```
4.15.0-46-generic
```



## 2. Install (our modified version of ) Mellanox OFED driver

**This is optional if you are using the user-space KRCore**. 

We first install the driver from a snapshot of the tarball in the artifact. Then we install our patches that support DCT.  If you are using machines provided by us, then no need for this step. 

The `MLNX_OFED` codes are under `${PROJECT_PATH}/mlnx-ofed-4.9-driver`. 

First, install the original driver (note that the old driver---if exists---should be removed first):

```sh
cd ${PROJECT_PATH}/mlnx-ofed-4.9-driver
wget https://www.mellanox.com/downloads/ofed/MLNX_OFED-4.9-3.1.5.0/MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64.tgz
tar zxf MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64 
sudo ./mlnxofedinstall
sudo /etc/init.d/openibd restart
```

Then, **reboot** your machine if no error prints.  If there is an error, please contact your 

```sh
sudo reboot -n
```

After that , add the patch to driver:

It will let you to assign one patch name, and just type-in any name as you like.

```sh
cd ${PROJECT_PATH}/mlnx-ofed-4.9-driver
sh build.sh
```

Finally, reboot again:

```sh
sudo reboot -n
```

Note: 

- Do not modify the tar file.

- If give the error message "the ib_dc_wr not found":

  - Please remove `/usr/src/ofa_kernel/default` first, then reinstall the patch

```
sudo rm -rf /usr/src/ofa_kernel/default
```

To check whether the OFED installed correctly, you can use `ibstatus`,  which should print something like:

```
Infiniband device 'mlx5_0' port 1 status:
	default gid:	 fe80:0000:0000:0000:ec0d:9a03:00ca:2f4c
	base lid:	 0x30
	sm lid:		 0xb
	state:		 4: ACTIVE
	phys state:	 5: LinkUp
	rate:		 100 Gb/sec (4X EDR)
	link_layer:	 InfiniBand
```

If the state is INIT, you can use `sudo /etc/init.d/opensmd start` to enable it. 

## 3. Other software packages 

#### 3.1 clang-9

If you are using machines provided by us, then no need for this step. 
(Optional) If running user-space libraries, install libclang-dev first: 
```
sudo apt install libclang-dev
```

There are two methods to install clang-9 on your machine. Choose one method as you like.

Method 1: install clang using `apt`

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 9
```

Method 2:(**Recommend**) install clang using source binary (assuming we are on ubuntu 16.04). 

```bash
# get source code
wget https://releases.llvm.org/9.0.0/clang+llvm-9.0.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz

# unzip the source file, rename it and add it to PATH directly. We make bash as an example.
tar -xvf clang+llvm-9.0.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
mv clang+llvm-9.0.0-x86_64-linux-gnu-ubuntu-16.04 clang-9

# Then set the PATH environment according to your downloaded path
# For example, if the clang-9 is under path ~/, set the PATH as:
#	CLANG_HOME=~/clang-9
# PATH=$CLANG_HOME/bin:$PATH
```

Note that the ubuntu would immediately return from the `.bashrc` if not running interactively, which KRCore leverages for automatic evaluations. So please comment these code in `.bashrc` (if exists).

```sh
# If not running interactively, don't do anything
case $- in
    *i*) ;;
      *) return;;
esac
```

---

#### 3.2 Rust related

Install the rustup and the toolchain (version `nightly-2023-01-07`)

```
curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh
source $HOME/.cargo/env
rustup default nightly-2023-01-07-x86_64-unknown-linux-gnu # switch to nightly-2023-01-07-aarch64-unknown-linux-gnu on ARM
rustup component add rust-src
```

Use `rustup toolchain list` to validate the correct toolchain has been installed.

```sh
rustup toolchain list

# The output result is supposed to be:
#   stable-x86_64-unknown-linux-gnu
#   nightly-2023-01-07-x86_64-unknown-linux-gnu (default)
```

## 4. Validate if preparations are all done

#### 4.1 Building KRCore kernel modules

Now we can enter `${PROJECT_PATH}/KRdmaKit-syscall` and conduct `make` .

```sh
cd ${PROJECT_PATH}/KRdmaKit-syscall
make
```

It should report no error. 

---

#### 4.2 Build all of the user benchmark tools

Now enter into path `${PROJECT_PATH}/exp` and conduct `make boost all` simply.

```sh
cd ${PROJECT_PATH}/exp
cmake .
make boost all -j
```

It should report no error. 

---

#### 4.3 Trouble shooting

- If shows the `clang` related error, you may try to re-download the clang-9. Sometimes the downloaded file can be broken.



## 5. Install 

```sh
cd ${PROJECT_PATH}/KRdmaKit-syscall

make install

# you can also rmmod this kernel module by:
make uninstall
```

Congratulations! Now you have built and installed KRCore on this server! 

Please kindly contact us if you fail to compile either the kernel module or these experiment binaries. If you made it, you can refer to [exp.md](exp.md) to start the evaluations.
