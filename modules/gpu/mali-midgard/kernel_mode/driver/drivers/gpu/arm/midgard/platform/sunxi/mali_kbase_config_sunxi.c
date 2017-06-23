/*
 * Copyright (C) 2016 Allwinner Technology Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Author: Albert Yu <yuxyun@allwinnertech.com>
 */
#include <linux/clk.h>
#include <linux/clk/sunxi.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <linux/ioport.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#define GPU_FREQ 600 /* MHz */

#define SMC_REG_BASE 0x3007000
#define SMC_GPU_DRM_REG (SMC_REG_BASE) + 0x54

struct clk       *gpu_pll;
struct clk       *gpu_core;

void __iomem *ioaddr;
	
static struct kbase_platform_config sunxi_platform_config;

struct kbase_platform_config *kbase_get_platform_config(void)
{
	return &sunxi_platform_config;
}

int kbase_platform_early_init(void)
{
	struct device_node *np_gpu;
	ioaddr = ioremap(SMC_GPU_DRM_REG, 1);

	np_gpu = of_find_compatible_node(NULL, NULL, "arm,mali-midgard");

	gpu_pll = of_clk_get(np_gpu, 0);
	if (IS_ERR_OR_NULL(gpu_pll)) {
		printk(KERN_ERR "GPU: Failed to get gpu pll!\n");
		BUG();
		return -1;
	}

	gpu_core = of_clk_get(np_gpu, 1);
	if (IS_ERR_OR_NULL(gpu_core)) {
		printk(KERN_ERR "GPU: Failed to get gpu core clock!\n");
		BUG();
		return -1;
	}

	if (clk_set_rate(gpu_pll, GPU_FREQ*1000*1000))
		printk(KERN_ERR "GPU: Failed to set the frequency of gpu pll!\n");

	if (clk_set_rate(gpu_core, GPU_FREQ*1000*1000))
		printk(KERN_ERR "GPU: Failed to set the frequency of gpu core clock!\n");

	if (clk_prepare_enable(gpu_pll)) {
		printk(KERN_ERR "GPU: Failed to enable gpu pll!\n");
		BUG();
		return -1;
	}

	if (clk_prepare_enable(gpu_core)) {
		printk(KERN_ERR "GPU: Failed to enable gpu core clock!\n");
		BUG();
		return -1;
	}

	return 0;
}

static int sunxi_protected_mode_enter(struct kbase_device *kbdev)
{
	writel(1, ioaddr);

	return 0;
}

static int sunxi_protected_mode_reset(struct kbase_device *kbdev)
{
	if (sunxi_periph_reset_assert(gpu_core))
		printk(KERN_ERR "GPU: Failed to assert!\n");

	writel(0, ioaddr);

	if (sunxi_periph_reset_deassert(gpu_core))
		printk(KERN_ERR "GPU: Failed to deassert!\n");

	return 0;
}

static bool sunxi_protected_mode_supported(struct kbase_device *kbdev)
{
	return true;
}

struct kbase_protected_ops sunxi_protected_ops = {
	.protected_mode_enter = sunxi_protected_mode_enter,
	.protected_mode_reset = sunxi_protected_mode_reset,
	.protected_mode_supported = sunxi_protected_mode_supported,
};
