#
#    glar150 - GL-AR150 demo using Zyre
#
#    Copyright (c) the Contributors as noted in the AUTHORS file.
#    This file is part of the Glar150 Project.
#
#    This Source Code Form is subject to the terms of the Mozilla Public
#    License, v. 2.0. If a copy of the MPL was not distributed with this
#    file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# To build with draft APIs, use "--with drafts" in rpmbuild for local builds or add
#   Macros:
#   %_with_drafts 1
# at the BOTTOM of the OBS prjconf
%bcond_with drafts
%if %{with drafts}
%define DRAFTS yes
%else
%define DRAFTS no
%endif
%define SYSTEMD_UNIT_DIR %(pkg-config --variable=systemdsystemunitdir systemd)
Name:           glar150
Version:        0.0.1
Release:        1
Summary:        gl-ar150 demo using zyre
License:        MPLv2
URL:            https://github.com/ZMQers/glar150
Source0:        %{name}-%{version}.tar.gz
Group:          System/Libraries
# Note: ghostscript is required by graphviz which is required by
#       asciidoc. On Fedora 24 the ghostscript dependencies cannot
#       be resolved automatically. Thus add working dependency here!
BuildRequires:  ghostscript
BuildRequires:  asciidoc
BuildRequires:  automake
BuildRequires:  autoconf
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  systemd-devel
BuildRequires:  systemd
%{?systemd_requires}
BuildRequires:  xmlto
BuildRequires:  zeromq-devel
BuildRequires:  czmq-devel
BuildRequires:  zyre-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
glar150 gl-ar150 demo using zyre.

%package -n libglar0
Group:          System/Libraries
Summary:        gl-ar150 demo using zyre shared library

%description -n libglar0
This package contains shared library for glar150: gl-ar150 demo using zyre

%post -n libglar0 -p /sbin/ldconfig
%postun -n libglar0 -p /sbin/ldconfig

%files -n libglar0
%defattr(-,root,root)
%{_libdir}/libglar.so.*

%package devel
Summary:        gl-ar150 demo using zyre
Group:          System/Libraries
Requires:       libglar0 = %{version}
Requires:       zeromq-devel
Requires:       czmq-devel
Requires:       zyre-devel

%description devel
gl-ar150 demo using zyre development tools
This package contains development files for glar150: gl-ar150 demo using zyre

%files devel
%defattr(-,root,root)
%{_includedir}/*
%{_libdir}/libglar.so
%{_libdir}/pkgconfig/libglar.pc
%{_mandir}/man3/*
%{_mandir}/man7/*

%prep

%setup -q

%build
sh autogen.sh
%{configure} --enable-drafts=%{DRAFTS} --with-systemd-units
make %{_smp_mflags}

%install
make install DESTDIR=%{buildroot} %{?_smp_mflags}

# remove static libraries
find %{buildroot} -name '*.a' | xargs rm -f
find %{buildroot} -name '*.la' | xargs rm -f

%files
%defattr(-,root,root)
%doc README.md
%{_bindir}/glard
%{_mandir}/man1/glard*
%config(noreplace) %{_sysconfdir}/glar150/glard.cfg
%{SYSTEMD_UNIT_DIR}/glard.service
%dir %{_sysconfdir}/glar150
%if 0%{?suse_version} > 1315
%post
%systemd_post glard.service
%preun
%systemd_preun glard.service
%postun
%systemd_postun_with_restart glard.service
%endif

%changelog
