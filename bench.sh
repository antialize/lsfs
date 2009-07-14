#!/bin/bash
DEV=/dev/foobar
MNT=/mount/x
SIZE=$((1024*15))
CNT=20000

function b() {
    echo ./bench $2 $CNT $SIZE
    ./bench $2 $CNT $SIZE
    echo     /usr/bin/time -o bench.out -a -f "%E $F $1" ./bench $2 $CNT $SIZE
    /usr/bin/time -o bench.out -a -f "%E $F $1" ./bench $2 $CNT $SIZE
}

function testfsi() {
    echo dd bs=$((1024*1024)) count=1 if=/dev/zero of=$MNT/blob
    dd bs=$((1024*1024)) count=1 if=/dev/zero of=$MNT/blob
    b $1 $MNT/blob
}

function testfs() {
    umount $MNT
    mount $DEV $MNT
    testfsi $1
    umount $MNT
}

mkfs.reiserfs -q $DEV
testfs "reiserfs"
 
mkfs.ext2 -q $DEV
testfs "ext2"

mkfs.ext2 -q -T largefile4 $DEV
testfs "ext2 largefile4"

mkfs.ext3 -q $DEV
testfs "ext3"

mkfs.ext3 -q -T largefile4 $DEV
testfs "ext3 largefile4"

#mkfs.ext4 -q $DEV
#mkfs.ext4 -q -T largefile4 $DEV

./mkfs.lsfs -f 2048 -c 8 $DEV
./lsfs.fuse $DEV $MNT
testfsi "lsfs.fuse"
fusermount -u $MNT


