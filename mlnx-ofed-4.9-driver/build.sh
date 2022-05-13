#!/bin/sh
rm -rf mlnx-ofed-kernel-4.9/
tar zxf mlnx-ofed-kernel_4.9.orig.tar.gz
#rm -rf mlnx-ofed-kernel-4.9/drivers
cp -r ./drivers mlnx-ofed-kernel-4.9
cp -r ./include mlnx-ofed-kernel-4.9

cd mlnx-ofed-kernel-4.9/
dpkg-source --commit
dpkg-buildpackage -us -uc
cd ..


mv mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all.deb mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all_new.deb
sudo dpkg -i  --force-overwrite mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all_new.deb
#sudo dpkg -i mlnx-ofed-kernel-dkms_4.4-OFED.4.4.2.0.7.1.gee7aa0e_all.deb





