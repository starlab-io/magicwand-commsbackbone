obj-m+=char_driver.o

EXTRA_CFLAGS := -I$(src)/../../../common
EXTRA_CFLAGS += -std=gnu99

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
