#!/bin/sh
# Script to create OnStream SC-x0 device nodes (major 206)
# Usage: Makedevs.sh [nos [path to dev]]
# $Id$
major=206
nrs=4
dir=/dev
test -z "$1" || nrs=$1
test -z "$2" || dir=$2
declare -i nr
nr=0
while test $nr -lt $nrs; do
  mknod $dir/osst$nr c $major $nr
  chown 0.disk $dir/osst$nr; chmod 660 $dir/osst$nr;
  mknod $dir/nosst$nr c $major $[nr+128]
  chown 0.disk $dir/nosst$nr; chmod 660 $dir/nosst$nr;
  mknod $dir/osst${nr}l c $major $[nr+32]
  chown 0.disk $dir/osst${nr}l; chmod 660 $dir/osst${nr}l;
  mknod $dir/nosst${nr}l c $major $[nr+160]
  chown 0.disk $dir/nosst${nr}l; chmod 660 $dir/nosst${nr}l;
  mknod $dir/osst${nr}m c $major $[nr+64]
  chown 0.disk $dir/osst${nr}m; chmod 660 $dir/osst${nr}m;
  mknod $dir/nosst${nr}m c $major $[nr+192]
  chown 0.disk $dir/nosst${nr}m; chmod 660 $dir/nosst${nr}m;
  mknod $dir/osst${nr}a c $major $[nr+96]
  chown 0.disk $dir/osst${nr}a; chmod 660 $dir/osst${nr}a;
  mknod $dir/nosst${nr}a c $major $[nr+224]
  chown 0.disk $dir/nosst${nr}a; chmod 660 $dir/nosst${nr}a;
  let nr+=1
done
