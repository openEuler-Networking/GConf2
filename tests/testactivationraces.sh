#! /bin/sh

## fork-bomb and see if we end up with duplicate gconfd 

COUNT=8
TMPDIR=/tmp/gconftestactivationraces

mkdir $TMPDIR || true

/bin/rm $TMPDIR/* || true

for I in `seq 1 $COUNT`; do
  TMPFILE=`mktemp $TMPDIR/SET.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --type=int --set /test/foo $I &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --shutdown &
  TMPFILE=`mktemp $TMPDIR/GET.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --get /test/foo &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN2.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --shutdown &
done

for I in `seq 1 $COUNT`; do
  TMPFILE=`mktemp $TMPDIR/SET2.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --type=int --set /test/foo $I &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN3.$I.XXXXXX` || exit 1
  ../gconf/gconftool-2 --shutdown &
done
