#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}
#TODO: Check if dir was created successfully

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
	echo "Building kernel for ${ARCH}"
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
	make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
	make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
	make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

cd "${OUTDIR}/linux-stable"
if [ ! -f "arch/${ARCH}/boot/Image" ]; then
	echo "Kernel Image not found. Build likely failed."
	exit 1
fi

echo "Adding the Image in outdir"
# Added this cp here
cp arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

ROOT_DIR=rootfs
mkdir -p "$OUTDIR/$ROOT_DIR"
cd "$OUTDIR/$ROOT_DIR"

mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin var/log

cd ${OUTDIR}

if [ ! -d "${OUTDIR}/busybox" ]
then
	git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
	make distclean 
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/${ROOT_DIR} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/${ROOT_DIR}/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/${ROOT_DIR}/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
echo "Copying dependencies into rootfs:"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
echo "${CROSS_COMPILE} sysroot is: ${SYSROOT}"
cp -av ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/${ROOT_DIR}/lib/

cp -av ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/${ROOT_DIR}/lib64/
cp -av ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/${ROOT_DIR}/lib64/
cp -av ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/${ROOT_DIR}/lib64/


# TODO: Make device nodes
# Null device Major 1 minor 3
sudo mknod -m 666 ${OUTDIR}/${ROOT_DIR}/dev/null c 1 3
# Console device Major 5 minor 1
sudo mknod -m 600 ${OUTDIR}/${ROOT_DIR}/dev/console c 5 1

# TODO: Clean and build the writer utility
FINDER_APP=${HOME}/coursera/assignment-1-PrayagKakoty/finder-app/
echo "Makeing writer.c"
cd ${FINDER_APP}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp -v ${FINDER_APP}/writer ${OUTDIR}/${ROOT_DIR}/home
cp -v ${FINDER_APP}/finder.sh ${OUTDIR}/${ROOT_DIR}/home
cp -v ${FINDER_APP}/finder-test.sh ${OUTDIR}/${ROOT_DIR}/home
mkdir ${OUTDIR}/${ROOT_DIR}/home/conf
cp -v ${FINDER_APP}/../conf/username.txt ${OUTDIR}/${ROOT_DIR}/home/conf
cp -v ${FINDER_APP}/../conf/assignment.txt ${OUTDIR}/${ROOT_DIR}/home/conf

cp -v ${FINDER_APP}/autorun-qemu.sh ${OUTDIR}/${ROOT_DIR}/home

#Modify finder-test.sh to look at conf/assignment.txt instead of ../conf/assignment.txt
sed -i 's|\.\./conf/assignment\.txt|conf/assignment.txt|g' "${OUTDIR}/${ROOT_DIR}/home/finder-test.sh"

# TODO: Chown the root directory
cd ${OUTDIR}/${ROOT_DIR}
sudo chown -R root:root *


# TODO: Create initramfs.cpio.gz

cd "$OUTDIR/$ROOT_DIR"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
