NAME = benchmark
DURATION = 2000

obj-m += $(NAME).o
$(NAME)-objs := src/worker.o src/$(NAME).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

load:
	insmod $(NAME).ko duration=$(DURATION)

unload:
	rmmod $(NAME).ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f Module.symvers modules.order

re: clean all
