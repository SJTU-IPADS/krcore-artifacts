### Init step (Assuming ubuntu16.04)
Use the tarbarll in the repo (recommended). 
- `tar zxf MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64.tgz`
- `cd MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64 `
- `sudo ./mlnxofedinstall --add-kernel-support`

Or install the default driver source code of this repo. 
- `wget https://www.mellanox.com/downloads/ofed/MLNX_OFED-4.9-3.1.5.0/MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64.tgz`
- `tar zxf MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64.tgz`
- `cd MLNX_OFED_LINUX-4.9-3.1.5.0-ubuntu16.04-x86_64 `
- `sudo ./mlnxofedinstall --add-kernel-support`

### Build kernel-space driver
Run `./build.sh`, and follows the steps prompted by the commandline.
ps: if the commandline prompts the patch name, than type any name is ok.

### Warning
Do not modify the tar file.

### Trouble shooting
What if `ib_dc_wr` is not found? 
- Please remove `/usr/src/ofa_kernel/default`, and reinstall the patch. 
