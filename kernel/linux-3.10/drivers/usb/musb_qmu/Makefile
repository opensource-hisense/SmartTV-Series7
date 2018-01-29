##
#	@author xibin 
#


QMU=y

musb_hdrc-objs := src/musb_core.o src/mt8560.o 
EXTRA_CFLAGS+=-I./drivers/usb/musb_qmu/include/
obj-$(CONFIG_USB_MTK_MUSB) += musb_hdrc.o
##
#       @brief object files of this module
#
ifeq ($(CONFIG_USB_MTK_MUSB_PERIPHERAL),y)
musb_hdrc-objs+=src/musb_gadget_ep0.o src/musb_gadget.o
EXTRA_CFLAGS+=-DCONFIG_USB_GADGET_MUSB_HDRC
EXTRA_CFLAGS+=-DCONFIG_USB_MUSB_PERIPHERAL
endif


ifeq ($(CONFIG_USB_MTK_MUSB_DMA),y)
musb_hdrc-objs+=src/musbhsdma.o
EXTRA_CFLAGS+=-DCONFIG_USB_INVENTRA_DMA
else
EXTRA_CFLAGS+=-DCONFIG_MUSB_PIO_ONLY
endif

EXTRA_CFLAGS+=-DCONFIG_MT8560

ifeq ($(USB_ADB), true)
#EXTRA_CFLAGS		+= -DUSB_DEVICE_MODE_SUPPORT
endif

ifndef USB_ADB_PORT
export USB_ADB_PORT := 0
endif

ifeq ($(USB_ADB_PORT), 0)
EXTRA_CFLAGS		+= -DMUSB_GADGET_PORT_NUM=0
endif
ifeq ($(USB_ADB_PORT), 1)
EXTRA_CFLAGS		+= -DMUSB_GADGET_PORT_NUM=1
endif
ifeq ($(USB_ADB_PORT), 2)
EXTRA_CFLAGS		+= -DMUSB_GADGET_PORT_NUM=2
endif
ifeq ($(USB_ADB_PORT), 3)
EXTRA_CFLAGS		+= -DMUSB_GADGET_PORT_NUM=3
endif

ifeq ($(LINUX_EMU_USB_CDC), true)
EXTRA_CFLAGS += -DLINUX_EMU_USB_CDC_SUPPORT
endif

ifeq ($(LINUX_USB_VDEC_FPGA_TEST), true)
EXTRA_CFLAGS += -DLINUX_USB_VDEC_FPGA_TEST
endif
