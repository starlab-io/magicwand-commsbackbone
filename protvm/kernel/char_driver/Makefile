obj-m+=char_driver.o

ccflags-y := -I$(src)/../../../common
ccflags-y += -std=gnu99
ccflags-y += -ggdb 
#ccflags-y +=  -DDEBUG # enable pr_debug

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
