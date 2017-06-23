
/*
 * mipi subdev driver module
 *
 * Copyright (c) 2017 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _SUNXI_MIPI__H_
#define _SUNXI_MIPI__H_

#include "../platform/platform_cfg.h"
#include "combo_rx/combo_rx_reg.h"

enum mipi_pad {
	MIPI_PAD_SINK,
	MIPI_PAD_SOURCE,
	MIPI_PAD_NUM,
};

struct combo_config {
	enum lvds_lane_num lvds_ln;
	unsigned int lane_num;
	unsigned int total_rx_ch;
};

struct combo_format {
	u32 code;
	enum lvds_bit_width bit_width;
};

struct mipi_dev {
	int use_cnt;
	struct v4l2_subdev subdev;
	struct media_pad mipi_pads[MIPI_PAD_NUM];
	struct platform_device *pdev;
	struct list_head mipi_list;
	unsigned int id;
	spinlock_t slock;
	struct mutex subdev_lock;
	void __iomem *base;
	unsigned int if_type;
	struct mipi_para csi2_cfg;
	struct mipi_fmt_cfg csi2_fmt;
	struct combo_config cmb_cfg;
	struct combo_format *cmb_fmt;
	enum combo_mipi_mode cmb_mipi_mode;
	enum combo_lvds_mode cmb_lvds_mode;
	enum combo_hispi_mode cmb_hispi_mode;
	struct v4l2_mbus_framefmt format;
};

struct v4l2_subdev *sunxi_mipi_get_subdev(int id);
int sunxi_mipi_platform_register(void);
void sunxi_mipi_platform_unregister(void);

#endif /*_SUNXI_MIPI__H_*/
