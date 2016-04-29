Name:           libbus1
Version:        1
Release:        3
Summary:        Bus1 IPC Library
License:        LGPL2+
URL:            https://github.com/bus1/libbus1
Source0:        %{name}.tar.xz
BuildRequires:  autoconf automake pkgconfig
BuildRequires:  c-rbtree-devel
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

%post
/sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%doc COPYING
%{_libdir}/libbus1.so.*

%files devel
%{_includedir}/org.bus1/*.h
%{_libdir}/libbus1.so
%{_libdir}/pkgconfig/libbus1.pc

%changelog
* Fri Apr 29 2016 <kay@redhat.com> 1-3
- bug fixes

* Tue Apr 26 2016 <kay@redhat.com> 1-2
- fix symbol export

* Mon Apr 25 2016 <kay@redhat.com> 1-1
- libbus1 1
