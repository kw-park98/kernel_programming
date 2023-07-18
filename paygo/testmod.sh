#!/bin/bash


# Ensure a module name was provided
#if [ -z "$1" ]; then
#    echo "Usage: $0 <module-name>"
#    exit 1
#fi

#NAME=${1%.c}
NAME="paygo"

clang-format-11 -i $NAME.c
clang-format-11 -i $NAME.h

# 1. make NAME=name
make NAME=$NAME
if [ $? -ne 0 ]; then
    echo "make failed"
    exit 1
fi
echo -e "\n"

# 2. insmod name.ko
sudo insmod $NAME.ko
if [ $? -ne 0 ]; then
    echo "insmod failed"
    exit 1
fi

sleep 5

# 3. rmmod name
sudo rmmod $NAME
if [ $? -ne 0 ]; then
    echo "rmmod failed"
    exit 1
fi

# 4. make clean
make clean
if [ $? -ne 0 ]; then
    echo "make clean failed"
    exit 1
fi
echo -e "\n"

# 5. dmesg | tail
#dmesg | tail
dmesg
