#
# spec file for package onstream (Version 0.9.13_0.7.3)
# 
# Copyright  (c)  2000  SuSE GmbH  Nuernberg, Germany.
#
# please send bugfixes or comments to feedback@suse.de.
#

# neededforbuild  gpp k_deflt lx_sus22 -lx_suse
# usedforbuild    -lx_suse aaa_base aaa_dir base bash bindutil binutils bison bzip compress cpio cracklib devs diff ext2fs file fileutil find flex gawk gcc gdbm gettext gpm gpp gppshare groff gzip k_deflt kbd less libc libz lx_sus22 lx_suse make mktemp modules ncurses net_tool netcfg nkita nkitb nssv1 pam patch perl pgp ps rcs rpm sendmail sh_utils shadow shlibs strace syslogd sysvinit texinfo textutil timezone unzip util vim xdevel xf86 xshared

Vendor:       SuSE GmbH, Nuernberg, Germany
Distribution: SuSE Linux 6.4 (i386)
Name:         onstream
Release:      0
Packager:     feedback@suse.de

Copyright:	GPL
Group:        Kernel
Autoreqprov:  on
Version:      0.9.13_0.7.9
Summary:      OnStream SC-x0 tape support tools
Source:	      onstream-20000524.tar.gz
#Patch:		onstream.dif
BuildRoot:	/var/tmp/%{name}-buildroot

%description
OnStream's SC-x0 tapes are not compliant with the SCSI2 spec for Serial
Access Storage Devices and can therefore not be operated by the Kernel's
SCSI Tape st driver st. This package contains some tools to allow to
test and access the device. The onstreamsg (osg) program allows reading
and writing of data to tapes. The osst driver is a kernel module
providing an st like interface. Some helpers are also included.
Note: Support for the IDE versions (DI-30) has been included in the
standard SuSE kernel.

Authors:
--------
    Terry Hardie <terryh@orcas.net>
    Willem Riede <wriede@monmouth.com>
    Kurt Garloff <garloff@suse.de>

SuSE series: ap

%define kversion %(uname -r)
%prep
%setup -n onstream
#%patch

%build
cd onstreamsg
make
cd ../tools
make
cd ../driver
test -e /usr/src/linux/.config || cp -p /boot/vmlinuz.config /usr/src/linux/.config
make

%install
install -d -o root -g root -m 755 $RPM_BUILD_ROOT/usr/bin
install -s -o root -g root -m 755 onstreamsg/osg $RPM_BUILD_ROOT/usr/bin/
install -s -o root -g root -m 755 tools/os_dump tools/os_write tools/stream $RPM_BUILD_ROOT/usr/bin/
cd driver
make install DESTDIR=$RPM_BUILD_ROOT 
%{?suse_check}

%files
/usr/bin/osg
/usr/bin/os_dump
/usr/bin/os_write
/usr/bin/stream
/lib/modules/%kversion/scsi/osst.o
/dev/osst*
/dev/nosst*
%doc README COPYING driver/README.osst driver/dev-reg.txt

%clean
rm -rf $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_DIR/%{name}

%changelog -n onstream
* Mon May 22 2000 - garloff@suse.de
- Update to 20000521 (0.7.3): Fixed read after seek error.
* Wed May 17 2000 - garloff@suse.de
- Update to 20000515 (0.7.2)
* Mon May 15 2000 - garloff@suse.de
- Update to 20000514 (0.7.1)
* Tue May 02 2000 - garloff@suse.de
- Update to 20000502 (0.7.0)
- Incorporated PPC patch into sources
- Use RPM_BUILD_ROOT
* Mon Apr 17 2000 - garloff@suse.de
- update to 20000417
* Tue Apr 04 2000 - garloff@suse.de
- Update to osst 0.6.3: Bugfix: Try to read all headers.
* Fri Mar 24 2000 - garloff@suse.de
- Updated to osst 0.6.2: Now releases all the SCSI commands ...
* Fri Mar 10 2000 - garloff@suse.de
- Bugfix from Terry on read error recovery: Go back, if located
  too far in osst.
* Thu Mar 09 2000 - garloff@suse.de
- Updated osst to 0.6.1: Fixed read error handling, and avoid
  READ_POSITION polling if possible.
- Minor bugfixes to osg and stream.
* Tue Mar 07 2000 - garloff@suse.de
- Updated osst to 0.60: Some error recovery is there now.
* Mon Mar 06 2000 - uli@suse.de
- onstreamsg.c now builds with -fsigned-char on PPC; to my surprise,
  apart from removing a signedness-related warning, this also untriggers
  an internal compiler error...
* Fri Mar 03 2000 - garloff@suse.de
- Initial creation of the package, including
  * onstreamsg: userspace driver (beta)
  * os_dump, os_write, stream: helpers
  * osst: kernelspace st like driver (alpha)
