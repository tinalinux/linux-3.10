/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __INCLUDES_H__
#define __INCLUDES_H__

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_irq.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/types.h>
#include <video/drv_hdmi.h>

#include "hdmi_core/hdmi_core.h"

#define FUNC_NAME __func__

/**
 * @short Main structures to instantiate the driver
 */
struct hdmi_tx_drv {
	struct platform_device		*pdev;
	/* Device node */
	struct device			*parent_dev;
	/* Device list */
	struct list_head		devlist;

	/* Interrupts */
	u32				irq;

	/* HDMI TX Controller */
	uintptr_t			reg_base;

	struct pinctrl			*pctl;

	struct clk			*hdmi_clk;
	struct clk			*hdmi_ddc_clk;
	struct clk			*hdmi_hdcp_clk;
	/*struct clk			*hdmi_cec_clk;*/

	struct work_struct		hdmi_work;
	struct work_struct		hpd_work;
	struct work_struct		hdcp_work;
	struct workqueue_struct		*hdmi_workqueue;

	struct mutex			ctrl_mutex;
	struct mutex			hpd_mutex;
	struct mutex			hdcp_mutex;

	struct hdmi_tx_core		*hdmi_core;
};

extern s32 disp_set_hdmi_func(struct disp_device_func *func);
extern void hdcp_core_enable(u8 enable);

#endif /* __INCLUDES_H__ */
