#!/bin/sh

# PATH already set
#export PATH=$PWD/tools/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian/bin:$PATH
export ARCH=arm
export KERNEL=kernel
export CROSS_COMPILE=arm-linux-gnueabihf-
export CONCURRENCY_LEVEL=$(nproc)
