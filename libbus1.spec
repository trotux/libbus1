Name:           libbus1
Version:        1
Release:        5%{?dist}
Summary:        Bus1 IPC Library
License:        LGPLv2+
URL:            https://github.com/bus1/libbus1
Source0:        https://github.com/bus1/libbus1/archive/v%{version}.tar.gz
BuildRequires:  autoconf automake pkgconfig
BuildRequires:  c-rbtree-devel
BuildRequires:  c-sundry-devel
BuildRequires:  c-variant-devel

%description
bus1 IPC Library

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license COPYING
%license LICENSE.LGPL2.1
%{_libdir}/libbus1.so.*

%files devel
%{_includedir}/org.bus1/*.h
%{_libdir}/libbus1.so
%{_libdir}/pkgconfig/libbus1.pc

%changelog
* Tue Jun 21 2016 <kay@redhat.com> 1-5
- update spec file according to Fedora guidelines

* Sun May 15 2016 <kay@redhat.com> 1-4
- use c-sundry

* Fri Apr 29 2016 <kay@redhat.com> 1-3
- bug fixes

* Tue Apr 26 2016 <kay@redhat.com> 1-2
- fix symbol export

* Mon Apr 25 2016 <kay@redhat.com> 1-1
- libbus1 1
