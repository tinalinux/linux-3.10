obj-$(CONFIG_SUNXI_DI)				= deinterlace.o
ifneq ($(CONFIG_ARCH_SUN50IW6P1)$(CONFIG_ARCH_SUN8IW12P1)$(CONFIG_ARCH_SUN8IW17P1),)
deinterlace-objs				:= di_v2x.o sunxi-di.o
else
deinterlace-objs				:= di_ebios.o di.o sunxi-di.o
endif
