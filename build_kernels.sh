#!/bin/bash -x

VERSION=$1

./build_kernel_JimboK.sh c2q $1

sleep 10

./build_kernel_JimboK.sh z3q $1

sleep 10

./build_kernel_JimboK.sh y2q $1

sleep 10

./build_kernel_JimboK.sh c1q $1

sleep 10

./build_kernel_JimboK.sh x1q $1

sleep 10

./build_kernel_JimboK.sh r8q $1

rm -rf out

make mrproper
