KERNEL_DIR = /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += hw6.o
CFLAGS=-Wall

default:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules

ledmon: ledmon.o
	$(CC) -o $@  $<

clean:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) clean
	rm ledmon.o ledmon
