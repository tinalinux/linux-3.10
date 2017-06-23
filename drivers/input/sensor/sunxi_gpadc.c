/*
 * drivers/input/sensor/sunxi_gpadc.c
 *
 * Copyright (C) 2016 Allwinner.
 * fuzhaoke <fuzhaoke@allwinnertech.com>
 *
 * SUNXI GPADC Controller Driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/pm.h>
#include "sunxi_gpadc.h"

static struct sunxi_gpadc *sunxi_gpadc;

/* clk_in: source clock, round_clk: sample rate */
void sunxi_gpadc_sample_rate_set(void __iomem *reg_base, u32 clk_in, u32 round_clk)
{
	u32 div, reg_val;
	if (round_clk > clk_in)
		pr_err("%s, invalid round clk!\n", __func__);
	div = clk_in / round_clk - 1 ;
	reg_val = readl(reg_base + GP_SR_REG);
	reg_val &= ~GP_SR_CON;
	reg_val |= (div << 16);
	writel(reg_val, reg_base + GP_SR_REG);
}

void sunxi_gpadc_ctrl_set(void __iomem *reg_base, u32 ctrl_para)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CTRL_REG);
	reg_val |= ctrl_para;
	writel(reg_val, reg_base + GP_CTRL_REG);
}

static u32 sunxi_gpadc_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CS_EN_REG);
	switch (id) {
	case GP_CH_0:
		reg_val |= GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val |= GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val |= GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val |= GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_CS_EN_REG);

	return 0;
}

static u32 sunxi_gpadc_ch_deselect(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CS_EN_REG);
	switch (id) {
	case GP_CH_0:
		reg_val &= ~GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val &= ~GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val &= ~GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val &= ~GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_CS_EN_REG);

	return 0;
}

static u32 sunxi_gpadc_ch_cmp(void __iomem *reg_base, enum gp_channel_id id,
							u32 low_uv, u32 hig_uv)
{
	u32 reg_val = 0, low = 0, hig = 0, unit = 0;

	if ((low_uv > hig_uv) || (hig_uv > VOL_RANGE)) {
		pr_err("%s, invalid compare value!", __func__);
		return -EINVAL;
	}

	/* anolog voltage range 0~1.8v, 12bits sample rate, unit=1.8v/(2^12) */
	unit = VOL_RANGE / 4096; /* 12bits sample rate */
	low = low_uv / unit;
	hig = hig_uv / unit;
	reg_val = low + (hig << 16);
	switch (id) {
	case GP_CH_0:
		writel(reg_val, reg_base + GP_CH0_CMP_DATA_REG);
		break;
	case GP_CH_1:
		writel(reg_val, reg_base + GP_CH1_CMP_DATA_REG);
		break;
	case GP_CH_2:
		writel(reg_val, reg_base + GP_CH2_CMP_DATA_REG);
		break;
	case GP_CH_3:
		writel(reg_val, reg_base + GP_CH3_CMP_DATA_REG);
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}

	return 0;
}

static u32 gpadc_cmp_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CS_EN_REG);
	switch (id) {
	case GP_CH_0:
		reg_val |= GP_CH0_CMP_EN;
		break;
	case GP_CH_1:
		reg_val |= GP_CH1_CMP_EN;
		break;
	case GP_CH_2:
		reg_val |= GP_CH2_CMP_EN;
		break;
	case GP_CH_3:
		reg_val |= GP_CH3_CMP_EN;
		break;
	default:
		pr_err("%s, invalid value!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_CS_EN_REG);

	return 0;
}

static u32 gpadc_cmp_deselect(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CTRL_REG);
	switch (id) {
	case GP_CH_0:
		reg_val &= ~GP_CH0_CMP_EN;
		break;
	case GP_CH_1:
		reg_val &= ~GP_CH1_CMP_EN;
		break;
	case GP_CH_2:
		reg_val &= ~GP_CH2_CMP_EN;
		break;
	case GP_CH_3:
		reg_val &= ~GP_CH3_CMP_EN;
		break;
	default:
		pr_err("%s, invalid value!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_CTRL_REG);

	return 0;
}

static u32 sunxi_gpadc_mode_select(void __iomem *reg_base, enum gp_select_mode mode)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_CTRL_REG);
	reg_val &= ~GP_MODE_SELECT;
	reg_val |= (mode << 18);
	writel(reg_val, reg_base + GP_CTRL_REG);

	return 0;
}

/* enable gpadc function, true:enable, false:disable */
static void sunxi_gpadc_enable(void __iomem *reg_base, bool onoff)
{
	u32 reg_val = 0;

	reg_val = readl(reg_base + GP_CTRL_REG);
	if (true == onoff)
		reg_val |= GP_ADC_EN;
	else
		reg_val &= ~GP_ADC_EN;
	writel(reg_val, reg_base + GP_CTRL_REG);
}
static u32 sunxi_enable_lowirq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_DATAL_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val |= GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val |= GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val |= GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val |= GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATAL_INTC_REG);

	return 0;
}
static u32 sunxi_disable_lowirq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_DATAL_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val &= ~GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val &= ~GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val &= ~GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val &= ~GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATAL_INTC_REG);

	return 0;
}
static u32 sunxi_enable_higirq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val = 0;


	reg_val = readl(reg_base + GP_DATAH_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val |= GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val |= GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val |= GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val |= GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATAH_INTC_REG);

	return 0;
}
static u32 sunxi_disable_higirq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val = 0;

	reg_val = readl(reg_base + GP_DATAH_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val &= ~GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val &= ~GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val &= ~GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val &= ~GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATAH_INTC_REG);

	return 0;
}
static u32 sunxi_enable_irq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_DATA_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val |= GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val |= GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val |= GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val |= GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATA_INTC_REG);

	return 0;
}
static u32 sunxi_disable_irq_ch_select(void __iomem *reg_base, enum gp_channel_id id)
{
	u32 reg_val;

	reg_val = readl(reg_base + GP_DATA_INTC_REG);
	switch (id) {
	case GP_CH_0:
		reg_val &= ~GP_CH0_SELECT;
		break;
	case GP_CH_1:
		reg_val &= ~GP_CH1_SELECT;
		break;
	case GP_CH_2:
		reg_val &= ~GP_CH2_SELECT;
		break;
	case GP_CH_3:
		reg_val &= ~GP_CH3_SELECT;
		break;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
	writel(reg_val, reg_base + GP_DATA_INTC_REG);

	return 0;
}

static u32 sunxi_ch_lowirq_status(void __iomem *reg_base)
{
	return readl(reg_base + GP_DATAL_INTS_REG);
}

static void sunxi_ch_lowirq_clear_flags(void __iomem *reg_base, u32 flags)
{
	writel(flags, reg_base + GP_DATAL_INTS_REG);
}

static u32 sunxi_ch_higirq_status(void __iomem *reg_base)
{
	return readl(reg_base + GP_DATAH_INTS_REG);
}

static void sunxi_ch_higirq_clear_flags(void __iomem *reg_base, u32 flags)
{
	writel(flags, reg_base + GP_DATAH_INTS_REG);
}

static u32 sunxi_ch_irq_status(void __iomem *reg_base)
{
	return readl(reg_base + GP_DATA_INTS_REG);
}

static void sunxi_ch_irq_clear_flags(void __iomem *reg_base, u32 flags)
{
	writel(flags, reg_base + GP_DATA_INTS_REG);
}

static u32 sunxi_gpadc_read_data(void __iomem *reg_base, enum gp_channel_id id)
{
	switch (id) {
	case GP_CH_0:
		return readl(reg_base + GP_CH0_DATA_REG) & GP_CH0_DATA_MASK;
	case GP_CH_1:
		return readl(reg_base + GP_CH1_DATA_REG) & GP_CH1_DATA_MASK;
	case GP_CH_2:
		return readl(reg_base + GP_CH2_DATA_REG) & GP_CH2_DATA_MASK;
	case GP_CH_3:
		return readl(reg_base + GP_CH3_DATA_REG) & GP_CH3_DATA_MASK;
	default:
		pr_err("%s, invalid channel id!", __func__);
		return -EINVAL;
	}
}

static int sunxi_gpadc_setup(struct platform_device *pdev,
					struct sunxi_gpadc *sunxi_gpadc)
{
	struct device_node *np = NULL;
	u32 channel_num;
	u32 val;
	int i;
	unsigned char name[30];
	u32 channel[5], channel_compare[5];
	u32 channel_compare_lowdata[5], channel_compare_higdata[5];

	np = pdev->dev.of_node;
	if (of_property_read_u32(np, "channel_num", &channel_num)) {
		pr_err("%s: get channel num failed\n", __func__);
		return -EBUSY;
	}
	for (i = 0; i < channel_num; i++) {
		sprintf(name, "channel%d", i);
		if (of_property_read_u32(np, name, &val)) {
			pr_err("%s:get %s err!\n", __func__, name);
			val = 0;
		}
		channel[i] = val;
		if (channel[i] == 1) {
			sunxi_gpadc_ch_select(sunxi_gpadc->reg_base, i);
			sunxi_enable_irq_ch_select(sunxi_gpadc->reg_base, i);
		} else {
			sunxi_gpadc_ch_deselect(sunxi_gpadc->reg_base, i);
			sunxi_disable_irq_ch_select(sunxi_gpadc->reg_base, i);
		}
	}
	for (i = 0; i < channel_num; i++) {
		sprintf(name, "channel%d_compare", i);
		if (of_property_read_u32(np, name, &val)) {
			pr_err("%s:get %s err!\n", __func__, name);
			val = 0;
		}
		channel_compare[i] = val;

		sprintf(name, "channel%d_compare_lowdata", i);
		if (of_property_read_u32(np, name, &val)) {
			pr_err("%s:get %s err!\n", __func__, name);
			val = 0;
		}
		channel_compare_lowdata[i] = val;

		sprintf(name, "channel%d_compare_higdata", i);
		if (of_property_read_u32(np, name, &val)) {
			pr_err("%s:get %s err!\n", __func__, name);
			val = 0;
		}
		channel_compare_higdata[i] = val;

		if (channel_compare[i] == 1) {
			gpadc_cmp_select(sunxi_gpadc->reg_base, i);
			sunxi_enable_lowirq_ch_select(sunxi_gpadc->reg_base, i);
			sunxi_enable_higirq_ch_select(sunxi_gpadc->reg_base, i);
			sunxi_gpadc_ch_cmp(sunxi_gpadc->reg_base, i,
				channel_compare_lowdata[i], channel_compare_higdata[i]);
		} else {
			gpadc_cmp_deselect(sunxi_gpadc->reg_base, i);
			sunxi_disable_lowirq_ch_select(sunxi_gpadc->reg_base, i);
			sunxi_disable_higirq_ch_select(sunxi_gpadc->reg_base, i);
			sunxi_gpadc_ch_cmp(sunxi_gpadc->reg_base, i, 0, 0);
		}
	}

	return 0;
}

static irqreturn_t sunxi_gpadc_interrupt(int irqno, void *dev_id)
{
	struct sunxi_gpadc *sunxi_gpadc = (struct sunxi_gpadc *)dev_id;
	u32  reg_val, reg_low, reg_hig;
	u32 data;

	reg_val = sunxi_ch_irq_status(sunxi_gpadc->reg_base);
	sunxi_ch_irq_clear_flags(sunxi_gpadc->reg_base, reg_val);
	reg_low = sunxi_ch_lowirq_status(sunxi_gpadc->reg_base);
	sunxi_ch_lowirq_clear_flags(sunxi_gpadc->reg_base, reg_low);
	reg_hig = sunxi_ch_higirq_status(sunxi_gpadc->reg_base);
	sunxi_ch_higirq_clear_flags(sunxi_gpadc->reg_base, reg_hig);

	if (reg_val & GP_CH0_DATA) {
		data = sunxi_gpadc_read_data(sunxi_gpadc->reg_base, GP_CH_0);
		input_event(sunxi_gpadc->input_dev, EV_MSC, MSC_SCAN, data);
		input_sync(sunxi_gpadc->input_dev);
	}
	if (reg_val & GP_CH1_DATA) {
		data = sunxi_gpadc_read_data(sunxi_gpadc->reg_base, GP_CH_1);
		data |= (0x1111 << 16);
		input_event(sunxi_gpadc->input_dev, EV_MSC, MSC_SCAN, data);
		input_sync(sunxi_gpadc->input_dev);
	}
	if (reg_val & GP_CH2_DATA) {
		data = sunxi_gpadc_read_data(sunxi_gpadc->reg_base, GP_CH_2);
		data |= (0x2222 << 16);
		input_event(sunxi_gpadc->input_dev, EV_MSC, MSC_SCAN, data);
		input_sync(sunxi_gpadc->input_dev);
	}
	if (reg_val & GP_CH3_DATA) {
		data = sunxi_gpadc_read_data(sunxi_gpadc->reg_base, GP_CH_3);
		data |= (0x3333 << 16);
		input_event(sunxi_gpadc->input_dev, EV_MSC, MSC_SCAN, data);
		input_sync(sunxi_gpadc->input_dev);
	}
	if (reg_low & GP_CH0_LOW)
		pr_debug("channel 0 low pend\n");

	if (reg_low & GP_CH1_LOW)
		pr_debug("channel 1 low pend\n");

	if (reg_low & GP_CH2_LOW)
		pr_debug("channel 2 low pend\n");

	if (reg_low & GP_CH3_LOW)
		pr_debug("channel 3 low pend\n");

	if (reg_hig & GP_CH0_HIG)
		pr_debug("channel 0 hight pend\n");

	if (reg_hig & GP_CH1_HIG)
		pr_debug("channel 1 hight pend\n");

	if (reg_hig & GP_CH2_HIG)
		pr_debug("channel 2 hight pend\n");

	if (reg_hig & GP_CH3_HIG)
		pr_debug("channel 3 hight pend\n");


	return IRQ_HANDLED;
}

int sunxi_gpadc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	static struct input_dev *input_dev;
	int ret = 0;

	if (!of_device_is_available(np)) {
		pr_err("%s: sunxi gpadc is disable\n", __func__);
		return -EPERM;
	}

	sunxi_gpadc = kzalloc(sizeof(struct sunxi_gpadc), GFP_KERNEL);
	if (IS_ERR_OR_NULL(sunxi_gpadc)) {
		pr_err("not enough memory for sunxi_gpadc\n");
		return -ENOMEM;
	}

	sunxi_gpadc->reg_base = of_iomap(np, 0);
	if (NULL == sunxi_gpadc->reg_base) {
		pr_err("sunxi_gpadc iomap fail\n");
		ret = -EBUSY;
		goto fail1;
	}

	sunxi_gpadc->irq_num = irq_of_parse_and_map(np, 0);
	if (0 == sunxi_gpadc->irq_num) {
		pr_err("sunxi_gpadc fail to map irq\n");
		ret = -EBUSY;
		goto fail2;
	}

	sunxi_gpadc->mclk = of_clk_get(np, 0);
	if (IS_ERR_OR_NULL(sunxi_gpadc->mclk)) {
		pr_err("sunxi_gpadc has no clk\n");
		ret = -EINVAL;
		goto fail3;
	} else{
		if (clk_prepare_enable(sunxi_gpadc->mclk)) {
			pr_err("enable sunxi_gpadc clock failed!\n");
			ret = -EINVAL;
			goto fail3;
		}
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("input_dev: not enough memory for input device\n");
		return -ENOMEM;
	}

	input_dev->name = GPADC_DEV_NAME;
	input_dev->phys = "sunxigpadc/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT_MASK(EV_MSC);
	set_bit(EV_MSC, input_dev->evbit);
	set_bit(MSC_SCAN, input_dev->mscbit);
	sunxi_gpadc->input_dev = input_dev;

	ret = input_register_device(sunxi_gpadc->input_dev);
	if (ret) {
		pr_err("input register fail\n");
		goto fail3;
	}
	sunxi_gpadc_sample_rate_set(sunxi_gpadc->reg_base, OSC_24MHZ, 50000);
	sunxi_gpadc_mode_select(sunxi_gpadc->reg_base, GP_CONTINUOUS_MODE);
	sunxi_gpadc_setup(pdev, sunxi_gpadc);
	sunxi_gpadc_enable(sunxi_gpadc->reg_base, true);
	if (request_irq(sunxi_gpadc->irq_num, sunxi_gpadc_interrupt,
			IRQF_TRIGGER_NONE, "sunxi-gpadc", sunxi_gpadc)) {
		pr_err("sunxi_gpadc request irq failure\n");
		return -1;
	}
	platform_set_drvdata(pdev, sunxi_gpadc);

	return 0;

fail3:
	free_irq(sunxi_gpadc->irq_num, sunxi_gpadc);
fail2:
	iounmap(sunxi_gpadc->reg_base);
fail1:
	kfree(sunxi_gpadc);

	return ret;
}

int sunxi_gpadc_remove(struct platform_device *pdev)

{
	struct sunxi_gpadc *sunxi_gpadc = platform_get_drvdata(pdev);

	sunxi_gpadc_enable(sunxi_gpadc->reg_base, false);
	free_irq(sunxi_gpadc->irq_num, sunxi_gpadc);
	clk_disable_unprepare(sunxi_gpadc->mclk);
	iounmap(sunxi_gpadc->reg_base);
	kfree(sunxi_gpadc);

	return 0;
}

#ifdef CONFIG_PM
static int sunxi_gpadc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_gpadc *sunxi_gpadc = platform_get_drvdata(pdev);

	disable_irq_nosync(sunxi_gpadc->irq_num);
	sunxi_gpadc_enable(sunxi_gpadc->reg_base, false);
	clk_disable_unprepare(sunxi_gpadc->mclk);

	return 0;
}

static int sunxi_gpadc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_gpadc *sunxi_gpadc = platform_get_drvdata(pdev);

	enable_irq(sunxi_gpadc->irq_num);
	sunxi_gpadc_enable(sunxi_gpadc->reg_base, true);
	clk_prepare_enable(sunxi_gpadc->mclk);

	return 0;
}

static const struct dev_pm_ops sunxi_gpadc_dev_pm_ops = {
	.suspend = sunxi_gpadc_suspend,
	.resume = sunxi_gpadc_resume,
};

#define SUNXI_GPADC_DEV_PM_OPS (&sunxi_gpadc_dev_pm_ops)
#else
#define SUNXI_GPADC_DEV_PM_OPS NULL
#endif

static struct of_device_id sunxi_gpadc_of_match[] = {
	{ .compatible = "allwinner,sunxi-gpadc"},
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_gpadc_of_match);

static struct platform_driver sunxi_gpadc_driver = {
	.probe  = sunxi_gpadc_probe,
	.remove = sunxi_gpadc_remove,
	.driver = {
		.name   = "sunxi-gpadc",
		.owner  = THIS_MODULE,
		.pm = SUNXI_GPADC_DEV_PM_OPS,
		.of_match_table = of_match_ptr(sunxi_gpadc_of_match),
	},
};
module_platform_driver(sunxi_gpadc_driver);

MODULE_AUTHOR("Fuzhaoke");
MODULE_DESCRIPTION("sunxi-gpadc driver");
MODULE_LICENSE("GPL");
