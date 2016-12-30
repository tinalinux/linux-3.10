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
#ifndef _SUNXI_LINUX_CLK_PERPARE_H
#define _SUNXI_LINUX_CLK_PERPARE_H

#ifdef CONFIG_SUNXI_CLK_PREPARE
extern int sunxi_clk_enable_prepare(struct clk *clk);
extern int sunxi_clk_disable_prepare(struct clk *clk);
#else
static int sunxi_clk_enable_prepare(struct clk *clk){return 0;};
static int sunxi_clk_disable_prepare(struct clk *clk){ return 0; };
#endif /* CONFIG_SUNXI_CLK_PREPARE */
#endif /* _SUNXI_LINUX_CLK_PERPARE_H */
