#! /bin/sh

## fork-bomb and see if we end up with duplicate gconfd 

COUNT=3
TMPDIR=/tmp/gconftestactivationraces

mkdir $TMPDIR

/bin/rm $TMPDIR/*

for I in `seq 1 $COUNT`; do
  TMPFILE=`mktemp $TMPDIR/SET.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --type=int --set /test/foo $I &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --shutdown &
  TMPFILE=`mktemp $TMPDIR/GET.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --get /test/foo &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN2.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --shutdown &
done

for I in `seq 1 $COUNT`; do
  TMPFILE=`mktemp $TMPDIR/SET2.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --type=int --set /test/foo $I &
  TMPFILE=`mktemp $TMPDIR/SHUTDOWN3.$I.XXXXXX` || exit 1
  strace -o $TMPFILE gconftool --shutdown &
done
