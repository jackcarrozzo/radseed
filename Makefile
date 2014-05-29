ifneq ($(KERNELRELEASE),)
	obj-m := radseed.o

# if not, provide the env here
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -fr radseed.ko radseed.mod.c radseed.mod.o radseed.o modules.order Module.symvers .radseed.* .tmp_versions 

