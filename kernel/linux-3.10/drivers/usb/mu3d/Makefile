ifeq ($(CONFIG_USB_MU3D_O0),y)
	EXTRA_CFLAGS += -O0
endif


obj-$(CONFIG_USB_MU3D_UDC)	+= udc-core.o
obj-$(CONFIG_USB_MU3D)		+= mu3d_hal/
obj-$(CONFIG_USB_MU3D_DRV)	+= mu3d_drv/
obj-$(CONFIG_USB_MU3D_TEST_DRV)	+= mu3d_test_drv/
