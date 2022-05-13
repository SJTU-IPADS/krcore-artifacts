#!/bin/bash
set -e

name=srp
version=$(grep "define _version" ${name}_spec_  | sed -e 's/.*_version //' | sed -e 's/}//' | sed -e 's/\s*//g')
release=$(grep "define _release" ${name}_spec_  | sed -e 's/.*_release //' | sed -e 's/}//' | sed -e 's/\s*//g')

/bin/cp -f ${name}_spec_ ${name}.spec
/bin/cp -f _makefile_ makefile
/bin/sed -i -r "s/^$name \(([0-9.-]+)\) (.*)/$name \($version-$release\) \2/" debian/changelog

if ! (grep -q "CONFIG_SCSI_SRP_ATTRS_STANDALONE" Kbuild 2>/dev/null); then
    echo "obj-\$(CONFIG_SCSI_SRP_ATTRS_STANDALONE)    += scsi/" >> Kbuild
fi
