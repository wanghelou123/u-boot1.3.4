#!/bin/bash

DEV_NAME=sde
BLOCK_CNT=`cat /sys/block/${DEV_NAME}/size`
IMAGE=u-boot.bin

if [ ${BLOCK_CNT} -le 0 ]; then
	echo "Error: NO media found in card reader."
	exit 1
fi

if [ ${BLOCK_CNT} -gt 32000000 ]; then
	echo "Error: Block device size (${BLOCK_CNT}) is too large"
	exit 1
fi

if [ ${BLOCK_CNT} -gt 4194304 ]; then
RESERVED=1024
echo "SDHC card."
else
RESERVED=0
echo "SD card."
fi

echo ${RESERVED}


let FIRMWARE1_POSITON=${BLOCK_CNT}-16-2-${RESERVED}
let FIRMWARE2_POSITON=${BLOCK_CNT}-16-2-1024-32-${RESERVED}
set -x
umount /dev/sdb1 2>/dev/null
umount /dev/sdb2 2>/dev/null
umount /dev/sdb3 2>/dev/null
umount /dev/sdb4 2>/dev/null

dd if=/dev/zero  of=/dev/${DEV_NAME} bs=512 seek=${FIRMWARE2_POSITON} count=1071
dd if=./${IMAGE} of=/dev/${DEV_NAME} bs=512 seek=${FIRMWARE1_POSITON}
dd if=./${IMAGE} of=/dev/${DEV_NAME} bs=512 seek=${FIRMWARE2_POSITON}
sync

