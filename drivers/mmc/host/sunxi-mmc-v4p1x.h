/*
 *
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __SUNXI_MMC_V4P1X_H__
#define __SUNXI_MMC_V4P1X_H__

void sunxi_mmc_init_priv_v4p1x(struct sunxi_mmc_host *host,
			       struct platform_device *pdev, int phy_index);
#endif
