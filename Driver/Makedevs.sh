#!/bin/sh
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
  mknod $dir/osstl$nr c $major $[nr+32]
  chown 0.disk $dir/osstl$nr; chmod 660 $dir/osstl$nr;
  mknod $dir/nosstl$nr c $major $[nr+160]
  chown 0.disk $dir/nosstl$nr; chmod 660 $dir/nosstl$nr;
  mknod $dir/osstm$nr c $major $[nr+64]
  chown 0.disk $dir/osstm$nr; chmod 660 $dir/osstm$nr;
  mknod $dir/nosstm$nr c $major $[nr+192]
  chown 0.disk $dir/nosstm$nr; chmod 660 $dir/nosstm$nr;
  mknod $dir/ossta$nr c $major $[nr+96]
  chown 0.disk $dir/ossta$nr; chmod 660 $dir/ossta$nr;
  mknod $dir/nossta$nr c $major $[nr+224]
  chown 0.disk $dir/nossta$nr; chmod 660 $dir/nossta$nr;
  let nr+=1
done
