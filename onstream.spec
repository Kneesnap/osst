#
# spec file for package onstream (Version 0.9.13_0.8.6.1)
# 
# Copyright  (c)  2000  SuSE GmbH  Nuernberg, Germany.
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
# 
# please send bugfixes or comments to feedback@suse.de.
#

# neededforbuild  gpp lx_suse
# usedforbuild    aaa_base aaa_dir autoconf automake base bash bindutil binutils bison bzip compress cpio cracklib db devs diffutils e2fsprogs file fileutils findutils flex gawk gcc gdbm gdbm-devel gettext glibc glibc-devel gpm gpp gppshare groff gzip kbd less libtool libz lx_suse make mktemp modutils ncurses ncurses-devel net-tools netcfg nkitb pam pam-devel patch perl pgp ps rcs rpm sendmail sh-utils shadow strace syslogd sysvinit texinfo textutils timezone unzip util-linux vim xdevel xf86 xshared

Vendor:       SuSE GmbH, Nuernberg, Germany
Distribution: SuSE Linux 7.0 (i386)
Name:         onstream
Packager:     feedback@suse.de

Copyright:	GPL
Group:        Base/Kernel
Autoreqprov:  on
Version:      0.9.13_0.8.6.1
Release:      0
Summary:      OnStream SC-x0 tape support tools
Source:	      onstream-0861-0942.tar.gz
#Patch:		onstream.dif
BuildRoot:	%{_tmppath}/%{name}-buildroot

%description
OnStream's SC-x0 tapes are not compliant with the SCSI2 spec for Serial
Access Storage Devices and can therefore not be operated by the Kernel's
SCSI Tape st driver st. This package contains some tools to allow to
test and access the device.
The osst driver is a kernel module providing an st like interface on the
device files /dev/osst*
The onstreamsg (osg) program allows reading and writing of data to tapes
via the sg interface. It is recommended to use the kernel driver, though.
Some helpers are also included.
Note: Support for the IDE versions (DI-30) has been included in the
standard SuSE kernel. However, the DI-x0 can be driven by osst via
ide-scsi. Similarily, the USB30 may be driven by osst via usb-storage,
if you have the latest 2.4.0 usb-storage driver providing the Freecom
support.
The ADRx0 series is fully SCSI-2 compliant  and supported by the standard
st driver.

Authors:
--------
    Willem Riede <wriede@monmouth.com>
    Terry Hardie <terryh@orcas.net>
    Kurt Garloff <garloff@suse.de>

SuSE series: ap

%define kversion %(uname -r)
%define kmodpath %(KVS=`uname -r`; KVS=${KVS#*.}; KVS=${KVS%%.*}; if test $KVS -gt 3; then echo kernel/drivers/scsi; else echo scsi; fi)
%prep
%setup -n onstream
#%patch

%build
cd onstreamsg
make
cd ../tools
make
#%ifnarch ia64
cd ..
if test ! -e /usr/src/linux/.config; then
  if -e /boot/vmlinuz.config; then cp -p /usr/src/linux/.config;
  else if -e /proc/config.gz; then gunzip -dc /proc/config.gz >/usr/src/linux/.config;
    else yes "" | make -C /usr/src/linux oldconfig
    fi
  fi
fi
make
#%endif

%install
install -d -o root -g root -m 755 $RPM_BUILD_ROOT/usr/bin
install -s -o root -g root -m 755 onstreamsg/osg $RPM_BUILD_ROOT/usr/bin/
install -s -o root -g root -m 755 tools/os_dump tools/os_write tools/stream $RPM_BUILD_ROOT/usr/bin/
#%ifnarch ia64
install -d $RPM_BUILD_ROOT/dev/
make install DESTDIR=$RPM_BUILD_ROOT 
#%endif
%{?suse_check}

%files
/usr/bin/osg
/usr/bin/os_dump
/usr/bin/os_write
/usr/bin/stream
#%ifnarch ia64
/lib/modules/%kversion/%{kmodpath}/osst.o
/dev/osst*
/dev/nosst*
#%endif
%doc README COPYING tapeinfo driver-24/README.osst driver-24/dev-reg.txt driver-24/osst-24.diff driver-22/osst-22.diff

%clean
rm -rf $RPM_BUILD_ROOT
rm -rf $RPM_BUILD_DIR/%{name}

%changelog -n onstream
* Wed Dec 20 2000 - garloff@suse.de
- Updated to 0.8.6.1/0.9.4.2:
  * Makefile fixes
  * IDE polling fixes
* Sat Oct 14 2000 - garloff@suse.de
- 20001004 (0.8.5): * Error recovery improvements.
  * Support USB and big-endian
  * Fixed error in frame number bookkeeping, so polling (needed
  for IDE and broken FW) works again.
  * Return value of rewind ioctl 0 now if successful
  * offl IOCTL now ejects the tape
  * Read the blocks 5-9,2990-2994,10- initially, as that's the
  order, they are preread by the FW.
* Tue Jul 25 2000 - garloff@suse.de
- 20000724 (0.8.1): Fix EOM handling (Give an early warning and
  report proper error code).
- Support 2.3/2.4 kernels (automatically detected). => Reenabled
  module on iA64.
* Tue Jun 27 2000 - garloff@suse.de
- 20000626 (0.8.0): Report MT_ISONSTREAM_SC tape,
  clean up osst_options.h
* Wed Jun 21 2000 - schwab@suse.de
- Don't compile kernel module on ia64.
* Wed Jun 21 2000 - garloff@suse.de
- 20000621 (0.7.99a): Use same OSST_SUPPORTS (SDp) from
  osst_detect.h in st and osst for both detect() and attach()
  Recreated patch against 2.2.16-SuSE.
* Mon May 29 2000 - garloff@suse.de
- 20000529 (0.7.91): Fix integration into kernel.
* Wed May 24 2000 - garloff@suse.de
- Version 20000524 (0.7.9):
- Now independent high-level driver
- Kernel patch
- Own device nodes (major 206)
- README.osst
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
