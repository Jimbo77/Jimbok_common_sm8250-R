#!/bin/bash -x

VERSION=$1

./build_kernel_JimboK.sh z3q

sleep 10

./build_kernel_JimboK.sh x1q

sleep 10

./build_kernel_JimboK.sh y2q

sleep 10

./build_kernel_JimboK.sh c1q

sleep 10

./build_kernel_JimboK.sh c2q

sleep 10

./build_kernel_JimboK.sh r8q

rm -rf out

make mrproper
