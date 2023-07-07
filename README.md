# kernel_programming
Codes of linux kernel modules

![Static Badge](https://img.shields.io/badge/linux-6.2.0-EABE41)
![Static Badge](https://img.shields.io/badge/ubuntu-18.0.4-C24F29)
![Static Badge](https://img.shields.io/badge/llvm-11.1.0-blue)

## 1) Installation
### Linux
```sh
apt-get install build-essential libncurses5 libncurses5-dev bin86 kernel-package libssl-dev bison flex libelf-dev

git clone --depth 1 --branch v6.2 git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

cd linux
make defconfig
make -j
make modules
make install
make modules_install
reboot
```

### Linter: clang-format
```sh
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 11
```

## 2) Execute Kernel Module
### 1. build
```sh
cd alloc_page
make
```
### 2. load
```sh
insmod alloc_page.ko
```
### 3. unload
```sh
rmmod alloc_page
```
### 4. clean
```sh
make clean
```

## 3) Contribution Guide
### 1. prepare
```sh
mkdir {module}
cd {module}
```
### 2. code
```sh
vi {module}.c
```
### 3. lint
```sh
clang-format-11 -i {module}.c
```
### 4. git
```sh
git branch -B {module}
git add .
git commit -m "[mod|doc|...]({module}): description"
git push origin {module}
```
