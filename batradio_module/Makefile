
# build module for rpi

# assume that PATH to crosscompiler is already set
# execute:   . setenv.h

ksrc   = "/home/sietse/Software/rpilinux"
sysr   = "/tmp/sysroot"
mdir   = "batsdr"

obj-m 	      += batradio.o 
batradio-objs := bradio.o  bradio_handler.o

all:
	make  -C $(ksrc) M=$(PWD) modules

# installeren op de pi zelf:
#   copieer naar /lib/modules/kernel_version/kernel/drivers/misc
#   depmod  kernel_version
#        of -a

modules_install:
	make  -C $(ksrc) M=$(PWD) INSTALL_MOD_PATH=$(sysr) INSTALL_MOD_DIR=$(mdir) modules_install

clean:
	make  -C $(ksrc) M=$(PWD) clean
