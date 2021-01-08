#!/bin/bash -x

CHIPSET_NAME=kona
VARIANT=$1

rm -rf out

ccache -M 4.5

export ARCH=arm64

make mrproper

mkdir out

DTS_DIR=$(pwd)/out/arch/$ARCH/boot/dts
BUILD_CROSS_COMPILE=$(pwd)/toolchains/aarch64-linux-android-4.9/bin/aarch64-linux-android-
#KERNEL_LLVM_BIN=$(pwd)/toolchain/llvm-arm-toolchain-ship/8.0/bin/clang
#KERNEL_LLVM_BIN=../toolchains/llvm/bin/clang
KERNEL_LLVM_BIN=$(pwd)/toolchains/llvm-arm-toolchain-ship-10.0/bin/clang
CLANG_TRIPLE=aarch64-linux-gnu-

KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc VARIANT_DEFCONFIG=vendor/$1/kona_sec_$1_usa_singlew_defconfig"

# If not cleaning the tree between builds, the following command will be
# required on 2nd and subsequent builds to prevent a huge slowdown of the
# build.
#
# find techpack -type f -name \*.o | xargs rm -f


#make $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE CFP_CC=$KERNEL_LLVM_BIN vendor/z3q_kor_singlex_defconfig

make O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE CFP_CC=$KERNEL_LLVM_BIN vendor/JimboK_defconfig

make -j$(nproc) O=$(pwd)/out $KERNEL_MAKE_ENV CROSS_COMPILE=$BUILD_CROSS_COMPILE REAL_CC=$KERNEL_LLVM_BIN CLANG_TRIPLE=$CLANG_TRIPLE CFP_CC=$KERNEL_LLVM_BIN

cp $(pwd)/out/arch/$ARCH/boot/Image.gz $(pwd)/out/Image.gz
cp $(pwd)/out/arch/$ARCH/boot/Image.gz-dtb $(pwd)/out/Image.gz-dtb

DTBO_FILES=$(find ${DTS_DIR}/samsung/ -name ${CHIPSET_NAME}-sec-*-r*.dtbo)

cat ${DTS_DIR}/vendor/qcom/*.dtb > $(pwd)/out/dtb.img

$(pwd)/tools/mkdtimg create $(pwd)/out/dtbo.img --page_size=4096 ${DTBO_FILES}

mv $(pwd)/out/Image.gz $(pwd)/out/Image.gz
mv $(pwd)/out/dtb.img $(pwd)/out/dtb.img

cp $(pwd)/out/Image.gz ~/build/OFRP/samsung/$1/prebuilt
cp $(pwd)/out/dtb.img ~/build/OFRP/samsung/$1/prebuilt


