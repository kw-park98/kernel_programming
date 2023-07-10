# lkm multicore-wise benchmark
Codes of linux kernel modules

## How to write a code
Write a code to worker.c.

## How to run
### 1) build
```sh
make
```
### 2) load
```sh
make load
```
You can set duration with `DURATION` option: `make DURATION=1000 load`.
### 3) unload
```sh
make unload
```
### 4) clean
```sh
make clean
```

