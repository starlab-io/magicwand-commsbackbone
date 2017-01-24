CONFIG_MODULE_SIG=n
EXTRA_CFLAGS := -std=gnu99

obj-m+=gnt_srvr.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
