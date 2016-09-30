obj-m+=char_driver.o

EXTRA_CFLAGS := -I$(src)/../../../rump_comms/common

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
