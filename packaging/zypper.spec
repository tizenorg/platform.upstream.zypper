Name:           zypper
BuildRequires:  libzypp-devel >= 14.27.0
BuildRequires:  boost-devel >= 1.33.1
BuildRequires:  gettext-devel >= 0.15
BuildRequires:  readline-devel >= 5.1
BuildRequires:  augeas-devel >= 0.5.0
BuildRequires:  gcc-c++ >= 4.5
BuildRequires:  cmake >= 2.4.6
Requires:       procps
Requires:       libzypp
License:        GPL-2.0+
Group:          System/Package Management
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Summary:        Command line software manager using libzypp
Version:        1.11.11
Release:        0
Source:         %{name}-%{version}.tar.bz2
Source1:        %{name}-rpmlintrc
Source1001: 	zypper.manifest
URL:            http://en.opensuse.org/Zypper

%description
Zypper is a command line tool for managing software. It can be used to add
package repositories, search for packages, install, remove, or update packages,
install patches, hardware drivers, verify dependencies, and more.

Zypper can be used interactively or non-interactively by user, from scripts,
or front-ends.

Authors:
--------
    Jan Kupec <jkupec@suse.cz>
    Duncan Mac-Vicar <dmacvicar@suse.de>
    Martin Vidner <mvidner@suse.cz>
    Josef Reidinger <jreidinger@suse.cz>

%package log
Requires:       python >= 2.7
BuildArch:      noarch
Summary:        CLI for accessing the zypper logfile
License:        GPL-2.0+
Group:          System/Packages
%description log
CLI for accessing the zypper logfile

Authors:
--------
    Dominik Heidler <dheidler@suse.de>

%prep
%setup -q
cp %{SOURCE1001} .

%build
mkdir build
cd build

# The code base is the same, but SLES11-SP1 (suse_version == 1110)
# may use it's own set of .po files from po/sle-zypper-po.tar.bz2.
unset TRANSLATION_SET

cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DSYSCONFDIR=%{_sysconfdir} \
      -DMANDIR=%{_mandir} \
      -DCMAKE_VERBOSE_MAKEFILE=TRUE \
      -DCMAKE_C_FLAGS_RELEASE:STRING="$RPM_OPT_FLAGS" \
      -DCMAKE_CXX_FLAGS_RELEASE:STRING="$RPM_OPT_FLAGS" \
      -DCMAKE_BUILD_TYPE=Release \
      -DUSE_TRANSLATION_SET=${TRANSLATION_SET:-zypper} \
      ..

#gettextize -f
make %{?_smp_mflags}
make -C po %{?_smp_mflags} translations

%install
cd build
make install DESTDIR=$RPM_BUILD_ROOT
make -C po install DESTDIR=$RPM_BUILD_ROOT
rm -f %{buildroot}/%{_bindir}/aptitude
# Create filelist with translations
cd ..
%{find_lang} zypper
%{__install} -d -m755 $RPM_BUILD_ROOT%{_var}/log
touch $RPM_BUILD_ROOT%{_var}/log/zypper.log
rm %{buildroot}%{_datadir}/doc/packages/zypper/COPYING

%lang_package

%files 
%manifest %{name}.manifest
%license COPYING
%defattr(-,root,root)
%config(noreplace) %{_sysconfdir}/zypp/zypper.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/zypper.lr
%config(noreplace) %{_sysconfdir}/logrotate.d/zypp-refresh.lr
%{_sysconfdir}/bash_completion.d/zypper.sh
%{_bindir}/zypper
%{_bindir}/installation_sources
%{_sbindir}/zypp-refresh
%dir %{_datadir}/zypper
%{_datadir}/zypper/zypper.aug
%dir %{_datadir}/zypper/xml
%{_datadir}/zypper/xml/xmlout.rnc
%doc %{_mandir}/man8/zypper.8*
%doc %dir %{_datadir}/doc/packages/zypper
%doc %{_datadir}/doc/packages/zypper/HACKING
# declare ownership of the log file but prevent
# it from being erased by rpm -e
%ghost %config(noreplace) %{_var}/log/zypper.log

%files log
%manifest %{name}.manifest
%defattr(-,root,root)
%{_sbindir}/zypper-log
%doc %{_mandir}/man8/zypper-log.8*


%changelog
