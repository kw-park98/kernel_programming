make
insmod module_test1.ko

sleep 2


rmmod module_test1

dmesg | tail


make clean

