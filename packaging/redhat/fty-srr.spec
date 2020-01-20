#
#    fty-srr - Save, restore and reset agent for 42ITy ecosystem
#
#    Copyright (C) 2014 - 2019 Eaton
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
Name:           fty-srr
Version:        1.0.0
Release:        1
Summary:        save, restore and reset agent for 42ity ecosystem
License:        GPL-2.0+
URL:            https://42ity.org
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
BuildRequires:  gcc-c++
BuildRequires:  fty-common-logging-devel
BuildRequires:  cxxtools-devel
BuildRequires:  fty-common-devel
BuildRequires:  fty-common-mlm-devel
BuildRequires:  fty-common-messagebus-devel
BuildRequires:  fty-common-dto-devel
BuildRequires:  protobuf-devel
BuildRequires:  fty-lib-certificate-devel
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
fty-srr save, restore and reset agent for 42ity ecosystem.

%package -n libfty_srr0
Group:          System/Libraries
Summary:        save, restore and reset agent for 42ity ecosystem shared library

%description -n libfty_srr0
This package contains shared library for fty-srr: save, restore and reset agent for 42ity ecosystem

%post -n libfty_srr0 -p /sbin/ldconfig
%postun -n libfty_srr0 -p /sbin/ldconfig

%files -n libfty_srr0
%defattr(-,root,root)
%{_libdir}/libfty_srr.so.*

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
%{_bindir}/fty-srr
%{_mandir}/man1/fty-srr*
%{_bindir}/fty-srr-cmd
%{_mandir}/man1/fty-srr-cmd*
%config(noreplace) %{_sysconfdir}/fty-srr/fty-srr.cfg
%{SYSTEMD_UNIT_DIR}/fty-srr.service
%dir %{_sysconfdir}/fty-srr
%if 0%{?suse_version} > 1315
%post
%systemd_post fty-srr.service
%preun
%systemd_preun fty-srr.service
%postun
%systemd_postun_with_restart fty-srr.service
%endif

%changelog
