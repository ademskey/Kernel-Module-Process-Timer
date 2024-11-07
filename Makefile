EXTRA_CFLAGS += -I/usr/src/linux-headers-$(shell uname -r)/include
APP_EXTRA_FLAGS:= -O2 -ansi -pedantic
KERNEL_SRC:= /lib/modules/$(shell uname -r)/build
SUBDIR= $(PWD)
GCC:=gcc
RM:=rm
.PHONY : clean

all: clean modules app

obj-m:= kmlab.o

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SUBDIR) modules

app: userapp.c userapp.h
	$(GCC) -o userapp userapp.c $(APP_EXTRA_FLAGS)

clean:
	$(RM) -f userapp *~ *.ko *.o *.mod.c Module.symvers modules.order
