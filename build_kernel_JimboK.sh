#!/bin/bash -x

CHIPSET_NAME=kona
VARIANT=$1
VERSION=$2

rm -rf out

ccache -M 4.5

export ARCH=arm64

make mrproper

mkdir out

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
BUILD_CROSS_COMPILE=aarch64-linux-gnu-
KERNEL_LLVM_BIN=$(pwd)/toolchains/llvm-arm-toolchain-ship-10.0/bin/clang
CLANG_TRIPLE=aarch64-linux-gnu-
BUILD_CROSS_COMPILE_COMPAT=arm-linux-gnueabi-

KERNEL_MAKE_ENV="ARCH=arm64 DTC_EXT=$(pwd)/tools/dtc VARIANT_DEFCONFIG=vendor/$1/kona_sec_$1_kor_singlex_defconfig"

make O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 CLANG_TRIPLE=$CLANG_TRIPLE vendor/JimboK_defconfig

make -j$(nproc) O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE CROSS_COMPILE_COMPAT=$BUILD_CROSS_COMPILE_COMPAT LLVM=1 CLANG_TRIPLE=$CLANG_TRIPLE

cp $(pwd)/out/arch/$ARCH/boot/Image.gz $(pwd)/out/Image.gz
cp $(pwd)/out/arch/$ARCH/boot/Image.gz-dtb $(pwd)/out/Image.gz-dtb

DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img

$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}

mv $(pwd)/out/Image.gz $(pwd)/out/Image-JimboK_$1.gz
mv $(pwd)/out/dtb.img $(pwd)/out/dtb-JimboK_$1.img

cp $(pwd)/out/Image-JimboK_$1.gz ~/build/mkbootimg-R
cp $(pwd)/out/dtb-JimboK_$1.img ~/build/mkbootimg-R
cp $(pwd)/out/Image.gz-dtb ~/build/mkbootimg-R
cp $(pwd)/out/dtbo.img ~/build/mkbootimg-R

~/build/mkbootimg-R/make_boot_$1.sh $2-$1
