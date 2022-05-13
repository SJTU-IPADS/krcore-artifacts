%if %{defined _cmake_version}
# If the version < 2.8.12: set the macro __cmake to 'cmake' (from path)
# instead of /usr/bin/cmake . We will provide newer one in the path
# perl has string comparison when prefixing with v:
%if %(perl -e 'print v%{_cmake_version} lt v2.8.12 ? "1" : "0"')
%global __cmake cmake
%endif
%endif

%{!?cmake: %global cmake cmake}
%{!?make_jobs: %global make_jobs make VERBOSE=1 %{?_smp_mflags}}
%{!?cmake_install: %global cmake_install DESTDIR=%{buildroot} make install}
%{!?_udevrulesdir: %global _udevrulesdir /etc/udev/rules.d}

%bcond_without libnl

# if systemd not supported, do not install the systemd service files
%global WITH_SYSTEMD %{defined _unitdir}
%if %{WITH_SYSTEMD} == 0
%global _unitdir /ignored
%endif

# build_docs: disabled by default
%bcond_with build_docs

# valgrind support: disabled by default; use "--with valgrind" to enable
%bcond_with valgrind

%if %{?rhel:%{rhel} < 8}%{?!rhel:0}
%global with_srp_compat 1
%endif
%if %{?suse_version:%{suse_version} < 1500}%{?!suse_version:0}
%global with_srp_compat 1
%endif

Name: rdma-core
Version: 50mlnx1
Release: 1%{?dist}
Summary: RDMA core userspace libraries and daemons
Group: System Environment/Libraries

# Almost everything is licensed under the OFA dual GPLv2, 2 Clause BSD license
#  providers/ipathverbs/ Dual licensed using a BSD license with an extra patent clause
#  providers/rxe/ Incorporates code from ipathverbs and contains the patent clause
#  providers/hfi1verbs Uses the 3 Clause BSD license
License: GPLv2 or BSD
Url: https://github.com/linux-rdma/rdma-core
Source: rdma-core-%{version}.tgz
# OFED: Build static libs by default.
%define with_static %{?_without_static: 0} %{?!_without_static: 1}
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

# 32-bit arm is missing required arch-specific memory barriers,
ExcludeArch: %{arm}

BuildRequires: binutils
BuildRequires: gcc
BuildRequires: libudev-devel
BuildRequires: pkgconfig
%if %{with libnl}
BuildRequires: pkgconfig(libnl-3.0)
BuildRequires: pkgconfig(libnl-route-3.0)
%endif
%if %{with valgrind}
BuildRequires: valgrind-devel
%endif
%if "%{WITH_SYSTEMD}" == "1"
BuildRequires: systemd
BuildRequires: systemd-devel
%endif
%if 0%{?fedora} >= 32
%define with_pyverbs %{?_with_pyverbs: 0} %{?!_with_pyverbs: 1}
%else
%define with_pyverbs %{?_with_pyverbs: 1} %{?!_with_pyverbs: 0}
%endif
%if %{with_pyverbs}
%if 0%{?rhel} == 7
BuildRequires: python36-devel
BuildRequires: python36-Cython
BuildRequires: cmake3
%global cmake %cmake3
%else
BuildRequires: cmake >= 2.8.11
BuildRequires: python3-devel
BuildRequires: python3-Cython
%endif
%else
%if 0%{?rhel} >= 8 || 0%{?fedora} >= 30
BuildRequires: python3
%else
BuildRequires: python
%endif
%endif

%if %{with build_docs}
%if 0%{?rhel} >= 8 || 0%{?fedora} >= 30 || %{with_pyverbs}
BuildRequires: python3-docutils
%else
BuildRequires: python-docutils
%endif
%endif

%if 0%{?fedora} >= 21 || 0%{?rhel} >= 8
BuildRequires: perl-generators
%endif

# Red Hat/Fedora previously shipped redhat/ as a stand-alone
# package called 'rdma', which we're supplanting here.
Provides: rdma = %{version}-%{release}
Obsoletes: rdma < %{version}-%{release}
Provides: rdma-ndd = %{version}-%{release}
Obsoletes: rdma-ndd < %{version}-%{release}
# the ndd utility moved from infiniband-diags to rdma-core
Conflicts: infiniband-diags <= 1.6.7
Requires: pciutils
# 32-bit arm is missing required arch-specific memory barriers,
ExcludeArch: %{arm}

%define CMAKE_FLAGS %{nil}
%if 0%{?suse_version}
# Tumbleweed's cmake RPM macro adds -Wl,--no-undefined to the module flags
# which is totally inappropriate and breaks building 'ENABLE_EXPORTS' style
# module libraries (eg ibacmp).
%define CMAKE_FLAGS -DCMAKE_MODULE_LINKER_FLAGS=""
%endif

%if 0%{?fedora} >= 25 || 0%{?rhel} >= 8
# pandoc was introduced in FC25, Centos8
%if %{with build_docs}
BuildRequires: pandoc
%endif
%endif

%description
RDMA core userspace infrastructure and documentation, including initialization
scripts, kernel driver-specific modprobe override configs, IPoIB network
scripts, dracut rules, and the rdma-ndd utility.

%package devel
Summary: RDMA core development libraries and headers
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: libibverbs%{?_isa} = %{version}-%{release}
Provides: libibverbs-devel = %{version}-%{release}
Obsoletes: libibverbs-devel < %{version}-%{release}
Provides: libibverbs-devel-static = %{version}-%{release}
Obsoletes: libibverbs-devel-static < %{version}-%{release}
Requires: libibumad%{?_isa} = %{version}-%{release}
Provides: libibumad-devel = %{version}-%{release}
Obsoletes: libibumad-devel < %{version}-%{release}
Provides: libibumad-static = %{version}-%{release}
Obsoletes: libibumad-static < %{version}-%{release}
Requires: librdmacm%{?_isa} = %{version}-%{release}
Provides: librdmacm-devel = %{version}-%{release}
Obsoletes: librdmacm-devel < %{version}-%{release}
Provides: librdmacm-static = %{version}-%{release}
Obsoletes: librdmacm-static < %{version}-%{release}
Requires: ibacm%{?_isa} = %{version}-%{release}
Provides: ibacm-devel = %{version}-%{release}
Obsoletes: ibacm-devel < %{version}-%{release}
Requires: infiniband-diags%{?_isa} = %{version}-%{release}
Provides: infiniband-diags-devel = %{version}-%{release}
Obsoletes: infiniband-diags-devel < %{version}-%{release}
Provides: libibmad-devel = %{version}-%{release}
Obsoletes: libibmad-devel < %{version}-%{release}
Provides: libibmad-static = %{version}-%{release}
Obsoletes: libibmad-static < %{version}-%{release}
%if %{with_static}
# Since our pkg-config files include private references to these packages they
# need to have their .pc files installed too, even for dynamic linking, or
# pkg-config breaks.
%if %{with libnl}
BuildRequires: pkgconfig(libnl-3.0)
BuildRequires: pkgconfig(libnl-route-3.0)
%endif
%endif
Provides: libcxgb3-static = %{version}-%{release}
Obsoletes: libcxgb3-static < %{version}-%{release}
Provides: libcxgb4-static = %{version}-%{release}
Obsoletes: libcxgb4-static < %{version}-%{release}
Provides: libhfi1-static = %{version}-%{release}
Obsoletes: libhfi1-static < %{version}-%{release}
Provides: libipathverbs-static = %{version}-%{release}
Obsoletes: libipathverbs-static < %{version}-%{release}
Provides: libmlx4-devel = %{version}-%{release}
Obsoletes: libmlx4-devel < %{version}-%{release}
Provides: libmlx4-static = %{version}-%{release}
Obsoletes: libmlx4-static < %{version}-%{release}
Provides: libmlx5-devel = %{version}-%{release}
Obsoletes: libmlx5-devel < %{version}-%{release}
Provides: libmlx5-static = %{version}-%{release}
Obsoletes: libmlx5-static < %{version}-%{release}
Provides: libnes-static = %{version}-%{release}
Obsoletes: libnes-static < %{version}-%{release}
Provides: libocrdma-static = %{version}-%{release}
Obsoletes: libocrdma-static < %{version}-%{release}
Provides: libi40iw-devel-static = %{version}-%{release}
Obsoletes: libi40iw-devel-static < %{version}-%{release}
Provides: libmthca-static = %{version}-%{release}
Obsoletes: libmthca-static < %{version}-%{release}

%description devel
RDMA core development libraries and headers.

%package -n infiniband-diags
Summary: InfiniBand Diagnostic Tools
Group: System Environment/Libraries
Provides: perl(IBswcountlimits)
Provides: libibmad = %{version}-%{release}
Obsoletes: libibmad < %{version}-%{release}
Obsoletes: openib-diags < 1.3

%description -n infiniband-diags
This package provides IB diagnostic programs and scripts needed to diagnose an
IB subnet.  infiniband-diags now also provides libibmad.  libibmad provides
low layer IB functions for use by the IB diagnostic and management
programs. These include MAD, SA, SMP, and other basic IB functions.

%package -n infiniband-diags-compat
Summary: OpenFabrics Alliance InfiniBand Diagnostic Tools
Group: System Environment/Libraries

%description -n infiniband-diags-compat
Deprecated scripts and utilities which provide duplicated functionality, most
often at a reduced performance. These are maintained for the time being for
compatibility reasons.

%package -n libibverbs
Summary: A library and drivers for direct userspace use of RDMA (InfiniBand/iWARP/RoCE) hardware
Group: System Environment/Libraries
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: %{name}%{?_isa} = %{version}-%{release}
Provides: libcxgb4 = %{version}-%{release}
Obsoletes: libcxgb4 < %{version}-%{release}
Provides: libefa = %{version}-%{release}
Obsoletes: libefa < %{version}-%{release}
Provides: libhfi1 = %{version}-%{release}
Obsoletes: libhfi1 < %{version}-%{release}
Provides: libi40iw = %{version}-%{release}
Obsoletes: libi40iw < %{version}-%{release}
Provides: libipathverbs = %{version}-%{release}
Obsoletes: libipathverbs < %{version}-%{release}
Provides: libmlx4 = %{version}-%{release}
Obsoletes: libmlx4 < %{version}-%{release}
%ifnarch s390x s390
Provides: libmlx5 = %{version}-%{release}
Obsoletes: libmlx5 < %{version}-%{release}
%endif
Provides: libmthca = %{version}-%{release}
Obsoletes: libmthca < %{version}-%{release}
Provides: libocrdma = %{version}-%{release}
Obsoletes: libocrdma < %{version}-%{release}
Provides: librxe = %{version}-%{release}
Obsoletes: librxe < %{version}-%{release}

%description -n libibverbs
libibverbs is a library that allows userspace processes to use RDMA
"verbs" as described in the InfiniBand Architecture Specification and
the RDMA Protocol Verbs Specification.  This includes direct hardware
access from userspace to InfiniBand/iWARP adapters (kernel bypass) for
fast path operations.

Device-specific plug-in ibverbs userspace drivers are included:

- libmlx4: Mellanox ConnectX-3 InfiniBand HCA
- libmlx5: Mellanox Connect-IB/X-4+ InfiniBand HCA

%package -n libibverbs-utils
Summary: Examples for the libibverbs library
Group: System Environment/Libraries
Requires: libibverbs%{?_isa} = %{version}-%{release}

%description -n libibverbs-utils
Useful libibverbs example programs such as ibv_devinfo, which
displays information about RDMA devices.

%package -n ibacm
Summary: InfiniBand Communication Manager Assistant
Group: System Environment/Libraries
%if "%{WITH_SYSTEMD}" == "1"
%{systemd_requires}
%endif
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: libibumad%{?_isa} = %{version}-%{release}
Requires: libibverbs%{?_isa} = %{version}-%{release}

%description -n ibacm
The ibacm daemon helps reduce the load of managing path record lookups on
large InfiniBand fabrics by providing a user space implementation of what
is functionally similar to an ARP cache.  The use of ibacm, when properly
configured, can reduce the SA packet load of a large IB cluster from O(n^2)
to O(n).  The ibacm daemon is started and normally runs in the background,
user applications need not know about this daemon as long as their app
uses librdmacm to handle connection bring up/tear down.  The librdmacm
library knows how to talk directly to the ibacm daemon to retrieve data.

%package -n libibumad
Summary: OpenFabrics Alliance InfiniBand umad (userspace management datagram) library
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description -n libibumad
libibumad provides the userspace management datagram (umad) library
functions, which sit on top of the umad modules in the kernel. These
are used by the IB diagnostic and management tools, including OpenSM.

%package -n librdmacm
Summary: Userspace RDMA Connection Manager
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: libibverbs%{?_isa} = %{version}-%{release}

%description -n librdmacm
librdmacm provides a userspace RDMA Communication Management API.

%package -n librdmacm-utils
Summary: Examples for the librdmacm library
Group: System Environment/Libraries
Requires: librdmacm%{?_isa} = %{version}-%{release}
Requires: libibverbs%{?_isa} = %{version}-%{release}

%description -n librdmacm-utils
Example test programs for the librdmacm library.

%package -n srp_daemon
Summary: Tools for using the InfiniBand SRP protocol devices
Group: System Environment/Libraries
Obsoletes: srptools < %{version}-%{release}
Provides: srptools = %{version}-%{release}
Obsoletes: openib-srptools <= 0.0.6
%if "%{WITH_SYSTEMD}" == "1"
%{systemd_requires}
%endif
Requires: %{name}%{?_isa} = %{version}-%{release}
Requires: libibumad%{?_isa} = %{version}-%{release}
Requires: libibverbs%{?_isa} = %{version}-%{release}

%description -n srp_daemon
In conjunction with the kernel ib_srp driver, srp_daemon allows you to
discover and use SCSI devices via the SCSI RDMA Protocol over InfiniBand.

%if %{with_pyverbs}
%package -n python3-pyverbs
Summary: Python3 API over IB verbs
Group: System Environment/Libraries
%{?python_provide:%python_provide python%{python3_pkgversion}-pyverbs}

%description -n python3-pyverbs
Pyverbs is a Cython-based Python API over libibverbs, providing an
easy, object-oriented access to IB verbs.
%endif

%prep
%setup

%build

# New RPM defines _rundir, usually as /run
%if 0%{?_rundir:1}
%else
%define _rundir /var/run
%endif

%{!?EXTRA_CMAKE_FLAGS: %global EXTRA_CMAKE_FLAGS %{nil}}

# Pass all of the rpm paths directly to GNUInstallDirs and our other defines.
%cmake %{CMAKE_FLAGS} \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_BINDIR:PATH=%{_bindir} \
         -DCMAKE_INSTALL_SBINDIR:PATH=%{_sbindir} \
         -DCMAKE_INSTALL_LIBDIR:PATH=%{_libdir} \
         -DCMAKE_INSTALL_LIBEXECDIR:PATH=%{_libexecdir} \
         -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=%{_localstatedir} \
         -DCMAKE_INSTALL_SHAREDSTATEDIR:PATH=%{_sharedstatedir} \
         -DCMAKE_INSTALL_INCLUDEDIR:PATH=%{_includedir} \
         -DCMAKE_INSTALL_INFODIR:PATH=%{_infodir} \
         -DCMAKE_INSTALL_MANDIR:PATH=%{_mandir} \
         -DCMAKE_INSTALL_SYSCONFDIR:PATH=%{_sysconfdir} \
         -DCMAKE_INSTALL_SYSTEMD_SERVICEDIR:PATH=%{_unitdir} \
         -DCMAKE_INSTALL_INITDDIR:PATH=%{_initrddir} \
         -DCMAKE_INSTALL_RUNDIR:PATH=%{_rundir} \
         -DCMAKE_INSTALL_DOCDIR:PATH=%{_docdir}/%{name}-%{version} \
         -DCMAKE_INSTALL_UDEV_RULESDIR:PATH=%{_udevrulesdir} \
         -DCMAKE_INSTALL_PERLDIR:PATH=%{perl_vendorlib} \
         -DENABLE_IBDIAGS_COMPAT:BOOL=True \
%if %{without libnl}
	-DENABLE_RESOLVE_NEIGH=0 \
%endif
%if "%{WITH_SYSTEMD}" == "0"
         -DWITHOUT_SYSTEMD=1 \
%endif
%if %{with_static}
         -DENABLE_STATIC=1 \
%endif
         %{EXTRA_CMAKE_FLAGS} \
%if %{defined __python3}
         -DPYTHON_EXECUTABLE:PATH=%{__python3} \
         -DCMAKE_INSTALL_PYTHON_ARCH_LIB:PATH=%{python3_sitearch} \
%endif
%if %{with srp_compat}
         -DENABLE_SRP_COMPAT=1 \
%endif
%if %{with_pyverbs}
         -DNO_PYVERBS=0
%else
	 -DNO_PYVERBS=1
%endif
%make_jobs

%install
%cmake_install
%if %{with_pyverbs}
mkdir pyverbs_docs
mv %{buildroot}%{_docdir}/%{name}-%{version}/tests pyverbs_docs/
%endif
rm -rf %{buildroot}%{_docdir}/%{name}-%{version}

mkdir -p %{buildroot}/%{_sysconfdir}/rdma

# Red Hat specific glue
%global dracutlibdir %{_prefix}/lib/dracut
%global sysmodprobedir %{_prefix}/lib/modprobe.d
mkdir -p %{buildroot}%{_sysconfdir}/udev/rules.d
mkdir -p %{buildroot}%{_libexecdir}
mkdir -p %{buildroot}%{_udevrulesdir}
mkdir -p %{buildroot}%{dracutlibdir}/modules.d/05rdma
mkdir -p %{buildroot}%{sysmodprobedir}
install -D -m0644 redhat/rdma.conf %{buildroot}/%{_sysconfdir}/rdma/rdma.conf
install -D -m0644 redhat/rdma.sriov-vfs %{buildroot}/%{_sysconfdir}/rdma/sriov-vfs
install -D -m0644 redhat/rdma.mlx4.conf %{buildroot}/%{_sysconfdir}/rdma/mlx4.conf
install -D -m0755 redhat/rdma.modules-setup.sh %{buildroot}%{dracutlibdir}/modules.d/05rdma/module-setup.sh
install -D -m0644 redhat/rdma.mlx4.sys.modprobe %{buildroot}%{sysmodprobedir}/libmlx4.conf
install -D -m0755 redhat/rdma.sriov-init %{buildroot}%{_libexecdir}/rdma-set-sriov-vf
install -D -m0755 redhat/rdma.mlx4-setup.sh %{buildroot}%{_libexecdir}/mlx4-setup.sh

# ibacm
%if %{with libnl}
IB_ACME=bin/ib_acme
[ -e build/bin/ib_acme ] && IB_ACME=build/bin/ib_acme
LD_LIBRARY_PATH=%{buildroot}%{_libdir} ${IB_ACME} -D . -O
# multi-lib conflict resolution hacks (bug 1429362)
sed -i -e 's|%{_libdir}|/usr/lib|' %{buildroot}%{_mandir}/man7/ibacm_prov.7
sed -i -e 's|%{_libdir}|/usr/lib|' ibacm_opts.cfg
install -D -m0644 ibacm_opts.cfg %{buildroot}%{_sysconfdir}/rdma/
%endif

if [ "%{_libexecdir}" != "/usr/libexec" ]; then
	sed -i -e 's|/usr/libexec|%{_libexecdir}|g' \
		%{buildroot}%{dracutlibdir}/modules.d/05rdma/module-setup.sh \
		%{buildroot}%{sysmodprobedir}/libmlx4.conf \
		#
fi

%if %{WITH_SYSTEMD} == 0
rm -rf %{buildroot}%{_unitdir}
%else
# Delete the package's init.d scripts
rm -rf %{buildroot}/%{_initrddir}/
rm -rf %{buildroot}/%{_sbindir}/srp_daemon.sh
%endif

%post -n rdma-core
# we ship udev rules, so trigger an update.
/sbin/udevadm trigger --subsystem-match=infiniband --action=change || true
/sbin/udevadm trigger --subsystem-match=net --action=change || true
/sbin/udevadm trigger --subsystem-match=infiniband_mad --action=change || true

%post -n infiniband-diags -p /sbin/ldconfig
%postun -n infiniband-diags -p /sbin/ldconfig

%post -n libibverbs -p /sbin/ldconfig
%postun -n libibverbs -p /sbin/ldconfig

%post -n libibumad -p /sbin/ldconfig
%postun -n libibumad -p /sbin/ldconfig

%post -n librdmacm -p /sbin/ldconfig
%postun -n librdmacm -p /sbin/ldconfig

%if "%{WITH_SYSTEMD}" == "1"
%post -n ibacm
%systemd_post ibacm.service
%preun -n ibacm
%systemd_preun ibacm.service
%postun -n ibacm
%systemd_postun_with_restart ibacm.service

%post -n srp_daemon
%systemd_post srp_daemon.service
%preun -n srp_daemon
%systemd_preun srp_daemon.service
%postun -n srp_daemon
%systemd_postun_with_restart srp_daemon.service
%endif

%files
%dir %{_sysconfdir}/rdma
%doc README.md
%doc Documentation/udev.md
%doc Documentation/tag_matching.md
%config(noreplace) %{_sysconfdir}/rdma/mlx4.conf
%config(noreplace) %{_sysconfdir}/rdma/rdma.conf
%config(noreplace) %{_sysconfdir}/rdma/sriov-vfs
%if 0
%config(noreplace) %{_sysconfdir}/modprobe.d/mlx4.conf
%config(noreplace) %{_sysconfdir}/modprobe.d/truescale.conf
%endif
%dir %{dracutlibdir}/modules.d/05rdma
%{dracutlibdir}/modules.d/05rdma/module-setup.sh
%if %{with libnl}
%{_udevrulesdir}/../rdma_rename
%endif
%{_udevrulesdir}/60-rdma-ndd.rules
%{_udevrulesdir}/60-rdma-persistent-naming.rules
%{_sysconfdir}/udev/rules.d/70-persistent-ipoib.rules
%{_udevrulesdir}/75-rdma-description.rules
%{_udevrulesdir}/90-rdma-umad.rules
%{sysmodprobedir}/libmlx4.conf
%{_libexecdir}/rdma-set-sriov-vf
%{_libexecdir}/mlx4-setup.sh
%if 0
%{_libexecdir}/truescale-serdes.cmds
%endif
%{_sbindir}/rdma-ndd
%if "%{WITH_SYSTEMD}" == "1"
%{_unitdir}/rdma-ndd.service
%endif
%{_mandir}/man8/rdma-ndd.*
%doc COPYING.*

%files devel
%doc MAINTAINERS
%dir %{_includedir}/infiniband
%dir %{_includedir}/rdma
%{_includedir}/infiniband/*
%{_includedir}/rdma/*
%if %{with_static}
%{_libdir}/lib*.a
%endif
%{_libdir}/lib*.so
%{_libdir}/pkgconfig/*.pc
%{_mandir}/man3/ibv_*
%{_mandir}/man3/rdma*
%{_mandir}/man3/umad*
%{_mandir}/man3/*_to_ibv_rate.*
%{_mandir}/man7/rdma_cm.*
%ifnarch s390x s390
%{_mandir}/man3/mlx5dv*
%endif
%{_mandir}/man3/mlx4dv*
%ifnarch s390x s390
%{_mandir}/man7/mlx5dv*
%endif
%{_mandir}/man7/mlx4dv*
%{_mandir}/man3/ibnd_*

%files -n infiniband-diags-compat
%{_sbindir}/ibcheckerrs
%{_mandir}/man8/ibcheckerrs*
%{_sbindir}/ibchecknet
%{_mandir}/man8/ibchecknet*
%{_sbindir}/ibchecknode
%{_mandir}/man8/ibchecknode*
%{_sbindir}/ibcheckport
%{_mandir}/man8/ibcheckport.*
%{_sbindir}/ibcheckportwidth
%{_mandir}/man8/ibcheckportwidth*
%{_sbindir}/ibcheckportstate
%{_mandir}/man8/ibcheckportstate*
%{_sbindir}/ibcheckwidth
%{_mandir}/man8/ibcheckwidth*
%{_sbindir}/ibcheckstate
%{_mandir}/man8/ibcheckstate*
%{_sbindir}/ibcheckerrors
%{_mandir}/man8/ibcheckerrors*
%{_sbindir}/ibdatacounts
%{_mandir}/man8/ibdatacounts*
%{_sbindir}/ibdatacounters
%{_mandir}/man8/ibdatacounters*
%{_sbindir}/ibdiscover.pl
%{_mandir}/man8/ibdiscover*
%{_sbindir}/ibswportwatch.pl
%{_mandir}/man8/ibswportwatch*
%{_sbindir}/ibqueryerrors.pl
%{_sbindir}/iblinkinfo.pl
%{_sbindir}/ibprintca.pl
%{_mandir}/man8/ibprintca*
%{_sbindir}/ibprintswitch.pl
%{_mandir}/man8/ibprintswitch*
%{_sbindir}/ibprintrt.pl
%{_mandir}/man8/ibprintrt*
%{_sbindir}/set_nodedesc.sh

%files -n infiniband-diags
%{_sbindir}/ibaddr
%{_sbindir}/ibnetdiscover
%{_sbindir}/ibping
%{_sbindir}/ibportstate
%{_sbindir}/ibroute
%{_sbindir}/ibstat
%{_sbindir}/ibsysstat
%{_sbindir}/ibtracert
%{_sbindir}/perfquery
%{_sbindir}/sminfo
%{_sbindir}/smpdump
%{_sbindir}/smpquery
%{_sbindir}/saquery
%{_sbindir}/vendstat
%{_sbindir}/iblinkinfo
%{_sbindir}/ibqueryerrors
%{_sbindir}/ibcacheedit
%{_sbindir}/ibccquery
%{_sbindir}/ibccconfig
%{_sbindir}/dump_fts
%{_sbindir}/ibhosts
%{_sbindir}/ibswitches
%{_sbindir}/ibnodes
%{_sbindir}/ibrouters
%{_sbindir}/ibfindnodesusing.pl
%{_sbindir}/ibidsverify.pl
%{_sbindir}/check_lft_balance.pl
%{_sbindir}/dump_lfts.sh
%{_mandir}/man8/dump_lfts*
%{_sbindir}/dump_mfts.sh
%{_mandir}/man8/dump_mfts*
%{_sbindir}/ibclearerrors
%{_mandir}/man8/ibclearerrors*
%{_sbindir}/ibclearcounters
%{_mandir}/man8/ibclearcounters*
%{_sbindir}/ibstatus
%{_libdir}/libibmad*.so.*
%{_libdir}/libibnetdisc*.so.*
%{perl_vendorlib}/IBswcountlimits.pm
%config(noreplace) %{_sysconfdir}/infiniband-diags/error_thresholds
%config(noreplace) %{_sysconfdir}/infiniband-diags/ibdiag.conf

%files -n libibverbs
%dir %{_sysconfdir}/libibverbs.d
%dir %{_libdir}/libibverbs
%{_libdir}/libibverbs*.so.*
%{_libdir}/libibverbs/*.so
%ifnarch s390x s390
%{_libdir}/libmlx5.so.*
%endif
%{_libdir}/libmlx4.so.*
%config(noreplace) %{_sysconfdir}/libibverbs.d/*.driver
%doc Documentation/libibverbs.md

%files -n libibverbs-utils
%{_bindir}/ibv_*
%{_mandir}/man1/ibv_*

%files -n ibacm
%if %{with libnl}
%config(noreplace) %{_sysconfdir}/rdma/ibacm_opts.cfg
%{_bindir}/ib_acme
%{_sbindir}/ibacm
%{_mandir}/man1/ibacm.*
%{_mandir}/man1/ib_acme.*
%{_mandir}/man7/ibacm.*
%{_mandir}/man7/ibacm_prov.*
%if "%{WITH_SYSTEMD}" == "1"
%{_unitdir}/ibacm.service
%{_unitdir}/ibacm.socket
%endif
%dir %{_libdir}/ibacm
%{_libdir}/ibacm/*
%endif
%doc Documentation/ibacm.md

%files -n libibumad
%{_libdir}/libibumad*.so.*

%files -n librdmacm
%{_libdir}/librdmacm*.so.*
%dir %{_libdir}/rsocket
%{_libdir}/rsocket/*.so*
%doc Documentation/librdmacm.md
%{_mandir}/man7/rsocket.*

%files -n librdmacm-utils
%{_bindir}/cmtime
%{_bindir}/mckey
%{_bindir}/rcopy
%{_bindir}/rdma_client
%{_bindir}/rdma_server
%{_bindir}/rdma_xclient
%{_bindir}/rdma_xserver
%{_bindir}/riostream
%{_bindir}/rping
%{_bindir}/rstream
%{_bindir}/ucmatose
%{_bindir}/udaddy
%{_bindir}/udpong
%{_mandir}/man1/cmtime.*
%{_mandir}/man1/mckey.*
%{_mandir}/man1/rcopy.*
%{_mandir}/man1/rdma_client.*
%{_mandir}/man1/rdma_server.*
%{_mandir}/man1/rdma_xclient.*
%{_mandir}/man1/rdma_xserver.*
%{_mandir}/man1/riostream.*
%{_mandir}/man1/rping.*
%{_mandir}/man1/rstream.*
%{_mandir}/man1/ucmatose.*
%{_mandir}/man1/udaddy.*
%{_mandir}/man1/udpong.*

%files -n srp_daemon
%config(noreplace) %{_sysconfdir}/srp_daemon.conf
%config(noreplace) %{_sysconfdir}/rdma/modules/srp_daemon.conf
%{_libexecdir}/srp_daemon/start_on_all_ports
%if "%{WITH_SYSTEMD}" == "1"
%{_unitdir}/srp_daemon.service
%{_unitdir}/srp_daemon_port@.service
%else
%{_initddir}/srpd
%{_sbindir}/srp_daemon.sh
%endif
%{_sbindir}/ibsrpdm
%{_sbindir}/srp_daemon
%{_sbindir}/run_srp_daemon
%{_udevrulesdir}/60-srp_daemon.rules
%{_mandir}/man1/ibsrpdm.1*
%{_mandir}/man5/srp_daemon.service.5*
%{_mandir}/man5/srp_daemon_port@.service.5*
%{_mandir}/man8/srp_daemon.8*
%doc Documentation/ibsrpdm.md

%if %{with_pyverbs}
%files -n python3-pyverbs
%{python3_sitearch}/pyverbs
%doc pyverbs_docs/*
%endif
