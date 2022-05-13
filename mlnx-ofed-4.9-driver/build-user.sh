#!/bin/sh
rm -rf libmlx5-41mlnx1
tar zxf libmlx5_41mlnx1.orig.tar.gz
#rm -rf mlnx-ofed-kernel-4.9/drivers
cp -r ./src libmlx5-41mlnx1

cd libmlx5-41mlnx1
dpkg-source --commit
dpkg-buildpackage -us -uc
cd ..

mv libmlx5-1_41mlnx1-OFED.4.9.0.1.2.49315_amd64.deb libmlx5-1_41mlnx1-OFED.4.9.0.1.2.49315_amd64_new.deb
sudo dpkg -i libmlx5-1_41mlnx1-OFED.4.9.0.1.2.49315_amd64_new.deb
#mv mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all.deb mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all_new.deb
#sudo dpkg -i  mlnx-ofed-kernel-dkms_4.9-OFED.4.9.3.1.5.1_all_new.deb
#sudo dpkg -i mlnx-ofed-kernel-dkms_4.4-OFED.4.4.2.0.7.1.gee7aa0e_all.deb



