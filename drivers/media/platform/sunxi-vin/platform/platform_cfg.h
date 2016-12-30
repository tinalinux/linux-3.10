
/*
 ******************************************************************************
 *
 * platform_cfg.h
 *
 * Hawkview ISP - platform_cfg.h module
 *
 * Copyright (c) 2014 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   2.0		  Yang Feng   	2014/07/24	      Second Version
 *
 ******************************************************************************
 */

#ifndef __PLATFORM_CFG__H__
#define __PLATFORM_CFG__H__

/*#define FPGA_VER*/
/*#define SUNXI_MEM*/

#ifndef FPGA_VER
#include <linux/clk.h>
#include <linux/clk/sunxi.h>
#include <linux/clk-private.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-sunxi.h>
#include <linux/regulator/consumer.h>
#endif

#include <linux/gpio.h>
#include <linux/sys_config.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <linux/slab.h>

#include "../utility/vin_os.h"

#ifdef FPGA_VER
#define DPHY_CLK (48*1000*1000)
#else
#define DPHY_CLK (150*1000*1000)
#endif

#if defined CONFIG_ARCH_SUN50IW1P1
#include "sun50iw1p1_vfe_cfg.h"
#define SUNXI_PLATFORM_ID ISP_PLATFORM_SUN50IW1P1
#elif defined CONFIG_ARCH_SUN8IW11P1
#include "sun8iw11p1_vfe_cfg.h"
#define SUNXI_PLATFORM_ID ISP_PLATFORM_NUM
#elif defined CONFIG_ARCH_SUN50IW3P1
#include "sun50iw3p1_vin_cfg.h"
#define SUNXI_PLATFORM_ID ISP_PLATFORM_NUM
#define DEFINE_VIND_REGS
#define DEFINE_VINC_REGS
#define DEFINE_VIPP_REGS
#define CROP_AFTER_SCALER
#elif defined CONFIG_ARCH_SUN50IW6P1
#include "sun50iw6p1_vin_cfg.h"
#define SUNXI_PLATFORM_ID ISP_PLATFORM_NUM
#define DEFINE_VIND_REGS
#define DEFINE_VINC_REGS
#define DEFINE_VIPP_REGS
#define CROP_AFTER_SCALER
#endif

struct mbus_framefmt_res {
	u32 res_pix_fmt;
	u32 res_mipi_bps;
};

#define CSI_CH_0	(1 << 20)
#define CSI_CH_1	(1 << 21)
#define CSI_CH_2	(1 << 22)
#define CSI_CH_3	(1 << 23)

#define MAX_DETECT_NUM	3

/*
 * The subdevices' group IDs.
 */
#define VIN_GRP_ID_SENSOR	(1 << 8)
#define VIN_GRP_ID_MIPI		(1 << 9)
#define VIN_GRP_ID_CSI		(1 << 10)
#define VIN_GRP_ID_ISP		(1 << 11)
#define VIN_GRP_ID_SCALER	(1 << 12)
#define VIN_GRP_ID_CAPTURE	(1 << 13)
#define VIN_GRP_ID_STAT		(1 << 14)

#define VIN_ALIGN_WIDTH 16
#define VIN_ALIGN_HEIGHT 16

#endif /*__PLATFORM_CFG__H__*/
