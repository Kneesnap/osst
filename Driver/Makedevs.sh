#!/bin/sh
major=206
nrs=4
test -z "$1" || nrs=$1
declare -i nr
nr=0
while test $nr -lt $nrs; do
  mknod /dev/osst$nr c $major $nr
  chown 0.disk /dev/osst$nr; chmod 660 /dev/osst$nr;
  mknod /dev/nosst$nr c $major $[nr+128]
  chown 0.disk /dev/nosst$nr; chmod 660 /dev/nosst$nr;
  mknod /dev/osstl$nr c $major $[nr+32]
  chown 0.disk /dev/osstl$nr; chmod 660 /dev/osstl$nr;
  mknod /dev/nosstl$nr c $major $[nr+160]
  chown 0.disk /dev/nosstl$nr; chmod 660 /dev/nosstl$nr;
  mknod /dev/osstm$nr c $major $[nr+64]
  chown 0.disk /dev/osstm$nr; chmod 660 /dev/osstm$nr;
  mknod /dev/nosstm$nr c $major $[nr+192]
  chown 0.disk /dev/nosstm$nr; chmod 660 /dev/nosstm$nr;
  mknod /dev/ossta$nr c $major $[nr+96]
  chown 0.disk /dev/ossta$nr; chmod 660 /dev/ossta$nr;
  mknod /dev/nossta$nr c $major $[nr+224]
  chown 0.disk /dev/nossta$nr; chmod 660 /dev/nossta$nr;
  let nr+=1
done
