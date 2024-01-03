ifneq ($(KERNELRELEASE),)
obj-m := kthread_semlck.o
EXTRA_CFLAGS = -DDEBUG

else
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

build:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
