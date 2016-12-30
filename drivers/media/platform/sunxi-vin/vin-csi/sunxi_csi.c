
/*
 ******************************************************************************
 *
 * sunxi_csi.c
 *
 * Hawkview ISP - sunxi_csi.c module
 *
 * Copyright (c) 2015 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   3.0		  Yang Feng   	2015/02/27	ISP Tuning Tools Support
 *
 ******************************************************************************
 */
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/sunxi_camera_v2.h>

#include "bsp_csi.h"
#include "parser_reg.h"
#include "sunxi_csi.h"
#include "../platform/platform_cfg.h"

#define CSI_MODULE_NAME "vin_csi"

#define IS_FLAG(x, y) (((x)&(y)) == y)

static LIST_HEAD(csi_drv_list);

#define CSI_MAX_WIDTH		0xffff
#define CSI_MAX_HEIGHT		0xffff

static struct csi_format sunxi_csi_formats[] = {
	{
		.code = V4L2_MBUS_FMT_YUYV8_2X8,
		.seq = SEQ_YUYV,
		.infmt = FMT_YUV422,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_YVYU8_2X8,
		.seq = SEQ_YVYU,
		.infmt = FMT_YUV422,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_UYVY8_2X8,
		.seq = SEQ_UYVY,
		.infmt = FMT_YUV422,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_VYUY8_2X8,
		.seq = SEQ_VYUY,
		.infmt = FMT_YUV422,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_YUYV8_1X16,
		.seq = SEQ_YUYV,
		.infmt = FMT_YUV422,
		.data_width = 16,
	}, {
		.code = V4L2_MBUS_FMT_YVYU8_1X16,
		.seq = SEQ_YVYU,
		.infmt = FMT_YUV422,
		.data_width = 16,
	}, {
		.code = V4L2_MBUS_FMT_UYVY8_1X16,
		.seq = SEQ_UYVY,
		.infmt = FMT_YUV422,
		.data_width = 16,
	}, {
		.code = V4L2_MBUS_FMT_VYUY8_1X16,
		.seq = SEQ_VYUY,
		.infmt = FMT_YUV422,
		.data_width = 16,
	}, {
		.code = V4L2_MBUS_FMT_SBGGR8_1X8,
		.infmt = FMT_RAW,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_SGBRG8_1X8,
		.infmt = FMT_RAW,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_SGRBG8_1X8,
		.infmt = FMT_RAW,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_SRGGB8_1X8,
		.infmt = FMT_RAW,
		.data_width = 8,
	}, {
		.code = V4L2_MBUS_FMT_SBGGR10_1X10,
		.infmt = FMT_RAW,
		.data_width = 10,
	}, {
		.code = V4L2_MBUS_FMT_SGBRG10_1X10,
		.infmt = FMT_RAW,
		.data_width = 10,
	}, {
		.code = V4L2_MBUS_FMT_SGRBG10_1X10,
		.infmt = FMT_RAW,
		.data_width = 10,
	}, {
		.code = V4L2_MBUS_FMT_SRGGB10_1X10,
		.infmt = FMT_RAW,
		.data_width = 10,
	}, {
		.code = V4L2_MBUS_FMT_SBGGR12_1X12,
		.infmt = FMT_RAW,
		.data_width = 12,
	}, {
		.code = V4L2_MBUS_FMT_SGBRG12_1X12,
		.infmt = FMT_RAW,
		.data_width = 12,
	}, {
		.code = V4L2_MBUS_FMT_SGRBG12_1X12,
		.infmt = FMT_RAW,
		.data_width = 12,
	}, {
		.code = V4L2_MBUS_FMT_SRGGB12_1X12,
		.infmt = FMT_RAW,
		.data_width = 12,
	}
};

static char *clk_name[CSI_CLK_NUM] = {
	"csi_core_clk",
	"csi_master_clk",
	"csi_misc_clk",
	"csi_core_clk_src",
	"csi_master_clk_24M_src",
	"csi_master_clk_pll_src",
};

static int __csi_clk_get(struct csi_dev *dev)
{
#ifndef FPGA_VER
	int i;
	int clk_index[CSI_CLK_NUM];
	struct device_node *np = dev->pdev->dev.of_node;

	of_property_read_u32_array(np, "clocks-index", clk_index, CSI_CLK_NUM);
	for (i = 0; i < CSI_CLK_NUM; i++) {
		if (clk_index[i] != NOCLK) {
			dev->clock[i] = of_clk_get(np, clk_index[i]);
			if (IS_ERR_OR_NULL(dev->clock[i]))
				vin_warn("Get clk Index:%d , Name:%s is NULL!\n",
					(int)clk_index[i], clk_name[i]);
		}
	}

	if (dev->clock[CSI_CORE_CLK] && dev->clock[CSI_CORE_CLK_SRC]) {
		if (clk_set_parent
		    (dev->clock[CSI_CORE_CLK], dev->clock[CSI_CORE_CLK_SRC])) {
			vin_err("sclk src Name:%s, core clock set parent failed\n",
				clk_name[CSI_CORE_CLK_SRC]);
			return -1;
		}
		if (clk_set_rate(dev->clock[CSI_CORE_CLK], CSI_CORE_CLK_RATE)) {
			vin_err("set core clock rate error\n");
			return -1;
		}
	}
	vin_log(VIN_LOG_CSI, "csi core clk = %ld\n",
		clk_get_rate(dev->clock[CSI_CORE_CLK]));
#endif
	return 0;
}

static int __csi_clk_enable(struct csi_dev *dev, int enable)
{
	int ret = 0;
#ifndef FPGA_VER
	int i;
	for (i = 0; i < CSI_CORE_CLK_SRC; i++) {
		if (CSI_MASTER_CLK != i) {
			if (dev->clock[i]) {
				if (enable) {
					if (clk_prepare_enable(dev->clock[i])) {
						vin_err("%s enable error\n",
							clk_name[i]);
						ret = -1;
					}
				} else {
					if (dev->clock[i]->enable_count != 0)
						clk_disable_unprepare(dev->clock[i]);
				}
			} else {
				vin_log(VIN_LOG_CSI, "%s is null\n", clk_name[i]);
			}
		}
	}
#endif
	return ret;
}

static void __csi_clk_release(struct csi_dev *dev)
{
#ifndef FPGA_VER
	int i;
	for (i = 0; i < CSI_CLK_NUM; i++) {
		if (dev->clock[i])
			clk_put(dev->clock[i]);
		else
			vin_log(VIN_LOG_CSI, "%s is null\n", clk_name[i]);
	}
#endif
}

static void __csi_clk_reset(struct csi_dev *dev, int enable)
{
#ifndef FPGA_VER
	if (enable)
		sunxi_periph_reset_assert(dev->clock[CSI_CORE_CLK]);
	else
		sunxi_periph_reset_deassert(dev->clock[CSI_CORE_CLK]);
#endif
}

static int __csi_pin_config(struct csi_dev *dev, int enable)
{
#ifndef FPGA_VER
	char pinctrl_names[10] = "";
	if (!IS_ERR_OR_NULL(dev->pctrl)) {
		devm_pinctrl_put(dev->pctrl);
	}
	if (1 == enable) {
		strcpy(pinctrl_names, "default");
	} else {
		strcpy(pinctrl_names, "sleep");
	}
	dev->pctrl = devm_pinctrl_get_select(&dev->pdev->dev, pinctrl_names);
	if (IS_ERR_OR_NULL(dev->pctrl)) {
		vin_err("csi%d request pinctrl handle failed!\n", dev->id);
		return -EINVAL;
	}
	usleep_range(5000, 6000);
#else
	void __iomem *gpio_base;
	vin_print("directly write pin config @ FPGA\n");

	gpio_base = ioremap(0x0300b000, 0x120);
	if (!gpio_base) {
		printk("gpio_base directly write pin config EIO\n");
		return -EIO;
	}
	writel(0x00330000, (gpio_base + 0x24));/*PB SDA SCL*/
	writel(0x33333333, (gpio_base + 0x90));/*PE CSI DATA*/
	writel(0x33333333, (gpio_base + 0x94));
	writel(0x33333333, (gpio_base + 0x98));
#endif
	return 0;
}

static int __csi_pin_release(struct csi_dev *dev)
{
#ifndef FPGA_VER
	devm_pinctrl_put(dev->pctrl);
#endif
	return 0;
}

static int __csi_set_fmt_hw(struct csi_dev *csi)
{
	struct v4l2_mbus_framefmt *mf = &csi->mf;
	int i;
#if defined(CONFIG_ARCH_SUN8IW11P1) || defined(CONFIG_ARCH_SUN50IW1P1)
	struct mbus_framefmt_res *res = (void *)mf->reserved;
	int ret;

	for (i = 0; i < csi->bus_info.ch_total_num; i++)
		csi->bus_info.bus_ch_fmt[i] = mf->code;
	for (i = 0; i < csi->bus_info.ch_total_num; i++) {
		csi->frame_info.pix_ch_fmt[i] = res->res_pix_fmt;
		csi->frame_info.ch_field[i] = mf->field;
		csi->frame_info.ch_size[i].width = mf->width;
		csi->frame_info.ch_size[i].height = mf->height;
		csi->frame_info.ch_offset[i].hoff = 0;
		csi->frame_info.ch_offset[i].voff = 0;
	}
	csi->frame_info.arrange = csi->arrange;

	ret = bsp_csi_set_fmt(csi->id, &csi->bus_info, &csi->frame_info);
	if (ret < 0) {
		vin_err("bsp_csi_set_fmt error at %s!\n", __func__);
		return -1;
	}
	ret = bsp_csi_set_size(csi->id, &csi->bus_info, &csi->frame_info);
	if (ret < 0) {
		vin_err("bsp_csi_set_size error at %s!\n", __func__);
		return -1;
	}
#else
	struct prs_ncsi_bt656_header bt656_header;
	struct prs_mcsi_if_cfg mcsi_if;
	struct prs_cap_mode mode;

	memset(&bt656_header, 0, sizeof(bt656_header));
	memset(&mcsi_if, 0, sizeof(mcsi_if));

	csi->ncsi_if.seq = csi->csi_fmt->seq;
	mcsi_if.seq = csi->csi_fmt->seq;

	switch (mf->field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		csi->ncsi_if.type = PROGRESSED;
		csi->ncsi_if.mode = FRAME_MODE;
		mcsi_if.mode = FRAME_MODE;
		break;
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		csi->ncsi_if.type = INTERLACE;
		csi->ncsi_if.mode = FIELD_MODE;
		mcsi_if.mode = FIELD_MODE;
		break;
	case V4L2_FIELD_INTERLACED:
		csi->ncsi_if.type = INTERLACE;
		csi->ncsi_if.mode = FRAME_MODE;
		mcsi_if.mode = FRAME_MODE;
		break;
	default:
		csi->ncsi_if.type = PROGRESSED;
		csi->ncsi_if.mode = FRAME_MODE;
		mcsi_if.mode = FRAME_MODE;
		break;
	}

	switch (csi->bus_info.bus_if) {
	case V4L2_MBUS_PARALLEL:
		if (csi->csi_fmt->data_width == 16)
			csi->ncsi_if.intf = PRS_IF_INTLV_16BIT;
		else
			csi->ncsi_if.intf = PRS_IF_INTLV;
		break;
	case V4L2_MBUS_BT656:
		if (csi->csi_fmt->data_width == 16) {
			if (csi->bus_info.ch_total_num == 1) {
				csi->ncsi_if.intf = PRS_IF_BT1120_1CH;
			} else if (csi->bus_info.ch_total_num == 2) {
				csi->ncsi_if.intf = PRS_IF_BT1120_2CH;
			} else if (csi->bus_info.ch_total_num == 4) {
				csi->ncsi_if.intf = PRS_IF_BT1120_4CH;
			}
		} else {
			if (csi->bus_info.ch_total_num == 1) {
				csi->ncsi_if.intf = PRS_IF_BT656_1CH;
			} else if (csi->bus_info.ch_total_num == 2) {
				csi->ncsi_if.intf = PRS_IF_BT656_2CH;
			} else if (csi->bus_info.ch_total_num == 4) {
				csi->ncsi_if.intf = PRS_IF_BT656_4CH;
			}
		}
		break;
	default:
		return -1;
	}

	switch (csi->bus_info.bus_if) {
	case V4L2_MBUS_PARALLEL:
		csic_prs_mode(csi->id, PRS_NCSI);
		csic_prs_ncsi_if_cfg(csi->id, &csi->ncsi_if);
		csic_prs_ncsi_en(csi->id, 1);
		break;
	case V4L2_MBUS_BT656:
		csic_prs_mode(csi->id, PRS_NCSI);
		bt656_header.ch0_id = 0;
		bt656_header.ch1_id = 1;
		bt656_header.ch2_id = 2;
		bt656_header.ch3_id = 3;
		csic_prs_ncsi_bt656_header_cfg(csi->id, &bt656_header);
		csic_prs_ncsi_en(csi->id, 1);
		break;
	case V4L2_MBUS_CSI2:
		csic_prs_mode(csi->id, PRS_MCSI);
		csic_prs_mcsi_if_cfg(csi->id, &mcsi_if);
		csic_prs_mcsi_en(csi->id, 1);
		break;
	default:
		return -1;
	}

	if (csi->capture_mode == V4L2_MODE_IMAGE)
		mode.mode = SCAP;
	else
		mode.mode = VCAP;

	if (csi->out_size.hor_len == 0 || csi->out_size.ver_len == 0) {
		csi->out_size.hor_len = mf->width;
		csi->out_size.ver_len = mf->height;
		csi->out_size.hor_start = 0;
		csi->out_size.ver_start = 0;
	}

	for (i = 0; i < csi->bus_info.ch_total_num; i++) {
		csic_prs_input_fmt_cfg(csi->id, i, csi->csi_fmt->infmt);
		csic_prs_output_size_cfg(csi->id, i, &csi->out_size);
	}

	csic_prs_capture_start(csi->id, csi->bus_info.ch_total_num, &mode);
#endif
	return 0;
}

static int sunxi_csi_subdev_s_power(struct v4l2_subdev *sd, int enable)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);

	if (enable) {
		__csi_clk_enable(csi, 1);
		__csi_clk_reset(csi, 0);
	} else {
		__csi_clk_enable(csi, 0);
		__csi_clk_reset(csi, 1);
	}
	__csi_pin_config(csi, enable);
	return 0;
}
static int sunxi_csi_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &csi->mf;

#if defined(CONFIG_ARCH_SUN8IW11P1) || defined(CONFIG_ARCH_SUN50IW1P1)
	if (enable) {
		bsp_csi_enable(csi->id);
		bsp_csi_disable(csi->id);
		bsp_csi_enable(csi->id);
		__csi_set_fmt_hw(csi);

		vin_log(VIN_LOG_CSI, "enable csi int in s_stream on\n");
		bsp_csi_int_clear_status(csi->id, csi->cur_ch,
					 CSI_INT_ALL);
		bsp_csi_int_enable(csi->id, csi->cur_ch,
				   CSI_INT_CAPTURE_DONE | CSI_INT_FRAME_DONE |
				   CSI_INT_BUF_0_OVERFLOW |
				   CSI_INT_BUF_1_OVERFLOW |
				   CSI_INT_BUF_2_OVERFLOW |
				   CSI_INT_HBLANK_OVERFLOW);

		if (csi->capture_mode == V4L2_MODE_IMAGE)
			bsp_csi_cap_start(csi->id, csi->bus_info.ch_total_num,
					  CSI_SCAP);
		else
			bsp_csi_cap_start(csi->id, csi->bus_info.ch_total_num,
					  CSI_VCAP);
	} else {
		vin_log(VIN_LOG_CSI, "disable csi int in s_stream off\n");
		bsp_csi_int_disable(csi->id, csi->cur_ch, CSI_INT_ALL);
		bsp_csi_int_clear_status(csi->id, csi->cur_ch,
					 CSI_INT_ALL);

		if (csi->capture_mode == V4L2_MODE_IMAGE)
			bsp_csi_cap_stop(csi->id, csi->bus_info.ch_total_num, CSI_SCAP);
		else
			bsp_csi_cap_stop(csi->id, csi->bus_info.ch_total_num, CSI_VCAP);
		bsp_csi_disable(csi->id);
	}
#else
	csic_prs_pclk_en(csi->id, enable);
	if (enable) {
		csic_prs_enable(csi->id);
		csic_prs_disable(csi->id);
		csic_prs_enable(csi->id);
		__csi_set_fmt_hw(csi);
	} else {
		switch (csi->bus_info.bus_if) {
		case V4L2_MBUS_PARALLEL:
		case V4L2_MBUS_BT656:
			csic_prs_ncsi_en(csi->id, 0);
			break;
		case V4L2_MBUS_CSI2:
			csic_prs_mcsi_en(csi->id, 0);
			break;
		default:
			return -1;
		}
		csic_prs_capture_stop(csi->id);
		csic_prs_disable(csi->id);
	}
#endif
	vin_print("%s on = %d, %d*%d %x %d\n", __func__, enable,
		mf->width, mf->height, mf->code, mf->field);

	return 0;
}
static int sunxi_csi_subdev_s_parm(struct v4l2_subdev *sd,
				   struct v4l2_streamparm *param)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	csi->capture_mode = param->parm.capture.capturemode;
	return 0;
}

static int sunxi_csi_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	return 0;
}

static struct csi_format *__csi_find_format(
	struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_csi_formats); i++)
		if (mf->code == sunxi_csi_formats[i].code)
			return &sunxi_csi_formats[i];
	return NULL;
}

static struct csi_format *__csi_try_format(
	struct v4l2_mbus_framefmt *mf)
{
	struct csi_format *csi_fmt;

	csi_fmt = __csi_find_format(mf);
	if (csi_fmt == NULL)
		csi_fmt = &sunxi_csi_formats[0];

	mf->code = csi_fmt->code;
	v4l_bound_align_image(&mf->width, 1, CSI_MAX_WIDTH, csi_fmt->wd_align,
			      &mf->height, 1, CSI_MAX_HEIGHT, 1, 0);
	return csi_fmt;
}

static struct v4l2_mbus_framefmt *__csi_get_format(
		struct csi_dev *csi, struct v4l2_subdev_fh *fh,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;

	return &csi->mf;
}

static int sunxi_csi_subdev_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_format *fmt)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	struct csi_format *csi_fmt;

	vin_log(VIN_LOG_FMT, "%s %d*%d %x %d\n", __func__,
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field);

	mf = __csi_get_format(csi, fh, fmt->which);

	if (fmt->pad == CSI_PAD_SOURCE) {
		if (mf) {
			mutex_lock(&csi->subdev_lock);
			fmt->format = *mf;
			mutex_unlock(&csi->subdev_lock);
		}
		return 0;
	}
	csi_fmt = __csi_try_format(&fmt->format);
	if (mf) {
		mutex_lock(&csi->subdev_lock);
		*mf = fmt->format;
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			csi->csi_fmt = csi_fmt;
		mutex_unlock(&csi->subdev_lock);
	}

	return 0;
}

static int sunxi_csi_subdev_get_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_format *fmt)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __csi_get_format(csi, fh, fmt->which);
	if (!mf)
		return -EINVAL;

	mutex_lock(&csi->subdev_lock);
	fmt->format = *mf;
	mutex_unlock(&csi->subdev_lock);
	return 0;
}

int sunxi_csi_subdev_init(struct v4l2_subdev *sd, u32 val)
{
	return 0;
}
static int sunxi_csi_subdev_set_crop(struct v4l2_subdev *sd,
				const struct v4l2_crop *crop)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	csi->out_size.hor_len = crop->c.width;
	csi->out_size.ver_len = crop->c.height;
	csi->out_size.hor_start = crop->c.left;
	csi->out_size.ver_start = crop->c.top;
	return 0;
}

static int sunxi_csi_s_mbus_config(struct v4l2_subdev *sd,
				   const struct v4l2_mbus_config *cfg)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);

	if (cfg->type == V4L2_MBUS_CSI2) {
		csi->bus_info.bus_if = V4L2_MBUS_CSI2;
		csi->bus_info.ch_total_num = 0;
		if (IS_FLAG(cfg->flags, V4L2_MBUS_CSI2_CHANNEL_0)) {
			csi->bus_info.ch_total_num++;
		}
		if (IS_FLAG(cfg->flags, V4L2_MBUS_CSI2_CHANNEL_1)) {
			csi->bus_info.ch_total_num++;
		}
		if (IS_FLAG(cfg->flags, V4L2_MBUS_CSI2_CHANNEL_2)) {
			csi->bus_info.ch_total_num++;
		}
		if (IS_FLAG(cfg->flags, V4L2_MBUS_CSI2_CHANNEL_3)) {
			csi->bus_info.ch_total_num++;
		}
		vin_print("csi->bus_info.ch_total_num = %d\n",
			  csi->bus_info.ch_total_num);
	} else if (cfg->type == V4L2_MBUS_PARALLEL) {
		csi->bus_info.bus_if = V4L2_MBUS_PARALLEL;
		if (IS_FLAG(cfg->flags, V4L2_MBUS_MASTER)) {
			if (IS_FLAG(cfg->flags, V4L2_MBUS_HSYNC_ACTIVE_HIGH)) {
				csi->bus_info.bus_tmg.href_pol = ACTIVE_HIGH;
				csi->ncsi_if.href = REF_POSITIVE;
			} else {
				csi->bus_info.bus_tmg.href_pol = ACTIVE_LOW;
				csi->ncsi_if.href = REF_NEGATIVE;
			}
			if (IS_FLAG(cfg->flags, V4L2_MBUS_VSYNC_ACTIVE_HIGH)) {
				csi->bus_info.bus_tmg.vref_pol = ACTIVE_HIGH;
				csi->ncsi_if.vref = REF_POSITIVE;
			} else {
				csi->bus_info.bus_tmg.vref_pol = ACTIVE_LOW;
				csi->ncsi_if.vref = REF_NEGATIVE;
			}
			if (IS_FLAG(cfg->flags, V4L2_MBUS_PCLK_SAMPLE_RISING)) {
				csi->bus_info.bus_tmg.pclk_sample = RISING;
				csi->ncsi_if.clk = CLK_RISING;
			} else {
				csi->bus_info.bus_tmg.pclk_sample = FALLING;
				csi->ncsi_if.clk = CLK_FALLING;
			}
			if (IS_FLAG(cfg->flags, V4L2_MBUS_FIELD_EVEN_HIGH)) {
				csi->bus_info.bus_tmg.field_even_pol = ACTIVE_HIGH;
				csi->ncsi_if.field = FIELD_POS;
			} else {
				csi->bus_info.bus_tmg.field_even_pol = ACTIVE_LOW;
				csi->ncsi_if.field = FIELD_NEG;
			}
		} else {
			vin_err("Do not support V4L2_MBUS_SLAVE!\n");
			return -1;
		}
	} else if (cfg->type == V4L2_MBUS_BT656) {
		csi->bus_info.bus_if = V4L2_MBUS_BT656;
		csi->bus_info.ch_total_num = 0;
		if (IS_FLAG(cfg->flags, CSI_CH_0))
			csi->bus_info.ch_total_num++;
		if (IS_FLAG(cfg->flags, CSI_CH_1))
			csi->bus_info.ch_total_num++;
		if (IS_FLAG(cfg->flags, CSI_CH_2))
			csi->bus_info.ch_total_num++;
		if (IS_FLAG(cfg->flags, CSI_CH_3))
			csi->bus_info.ch_total_num++;
		if (csi->bus_info.ch_total_num == 4) {
			csi->arrange.column = 2;
			csi->arrange.row = 2;
		} else if (csi->bus_info.ch_total_num == 2) {
			csi->arrange.column = 2;
			csi->arrange.row = 1;
		} else {
			csi->bus_info.ch_total_num = 1;
			csi->arrange.column = 1;
			csi->arrange.row = 1;
		}
		vin_print("ch_total_num = %d\n", csi->bus_info.ch_total_num);
		if (IS_FLAG(cfg->flags, V4L2_MBUS_PCLK_SAMPLE_RISING)) {
			csi->bus_info.bus_tmg.pclk_sample = RISING;
			csi->ncsi_if.clk = CLK_RISING;
		} else {
			csi->bus_info.bus_tmg.pclk_sample = FALLING;
			csi->ncsi_if.clk = CLK_FALLING;
		}
	}

	return 0;
}

static long sunxi_csi_subdev_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
				   void *arg)
{
#if defined(CONFIG_ARCH_SUN8IW11P1) || defined(CONFIG_ARCH_SUN50IW1P1)
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	switch (cmd) {
	/*CSI internal IOCTL*/
	case VIDIOC_SUNXI_CSI_SET_CORE_CLK:
		mutex_lock(&csi->subdev_lock);
		clk_set_rate(csi->clock[CSI_CORE_CLK], *(unsigned long *)arg);
		vin_print("Set core clk = %ld, after Set core clk = %ld\n",
			*(u64 *)arg, clk_get_rate(csi->clock[CSI_CORE_CLK]));
		mutex_unlock(&csi->subdev_lock);
		break;
	case VIDIOC_SUNXI_CSI_SET_M_CLK:
		break;
	/*CSI external IOCTL*/
	case VIDIOC_VIN_CSI_ONOFF:
		if (*(unsigned long *)arg)
			csi_enable(csi->id);
		else
			csi_disable(csi->id);
		break;
	case VIDIOC_VIN_CSI_IF_CFG:
		csi_if_cfg(csi->id, (struct csi_if_cfg *)arg);
		break;
	case VIDIOC_VIN_CSI_TIMING_CFG:
		csi_timing_cfg(csi->id, (struct csi_timing_cfg *)arg);
		break;
	case VIDIOC_VIN_CSI_FMT_CFG: {
		struct vin_csi_fmt_cfg *fc = (struct vin_csi_fmt_cfg *)arg;
		csi_fmt_cfg(csi->id, fc->ch, &fc->fmt);
		csi_set_size(csi->id, fc->ch, fc->so.length_h, fc->so.length_v,
				fc->so.length_y, fc->so.length_c);
		csi_set_offset(csi->id, fc->ch, fc->so.start_h, fc->so.start_v);
		break;
	}

	case VIDIOC_VIN_CSI_BUF_ADDR_CFG: {
		struct vin_csi_buf_cfg *bc = (struct vin_csi_buf_cfg *)arg;
		if (bc->set)
			csi_set_buffer_address(csi->id, bc->ch,
				bc->buf.buf_sel, bc->buf.addr);
		else
			bc->buf.addr = csi_get_buffer_address(csi->id, bc->ch,
						bc->buf.buf_sel);
		break;
	}

	case VIDIOC_VIN_CSI_CAP_MODE_CFG: {
		struct vin_csi_cap_mode *cm = (struct vin_csi_cap_mode *)arg;
		if (cm->on)
			csi_capture_start(csi->id, cm->total_ch, cm->mode);
		else
			csi_capture_stop(csi->id, cm->total_ch, cm->mode);
		break;
	}

	case VIDIOC_VIN_CSI_CAP_STATUS: {
		struct vin_csi_cap_status *cs = (struct vin_csi_cap_status *)arg;
		csi_capture_get_status(csi->id, cs->ch, &cs->status);
		break;
	}

	case VIDIOC_VIN_CSI_INT_CFG: {
		struct vin_csi_int_cfg *ic = (struct vin_csi_int_cfg *)arg;
		if (ic->en)
			csi_int_enable(csi->id, ic->ch, ic->sel);
		else
			csi_int_disable(csi->id, ic->ch, ic->sel);
		break;
	}
	default:
		return -ENOIOCTLCMD;
	}
#endif
	return 0;
}

static const struct v4l2_subdev_core_ops sunxi_csi_core_ops = {
	.s_power = sunxi_csi_subdev_s_power,
	.init = sunxi_csi_subdev_init,
	.ioctl = sunxi_csi_subdev_ioctl,
};

static const struct v4l2_subdev_video_ops sunxi_csi_subdev_video_ops = {
	.s_stream = sunxi_csi_subdev_s_stream,
	.s_crop = sunxi_csi_subdev_set_crop,
	.s_mbus_config = sunxi_csi_s_mbus_config,
	.s_parm = sunxi_csi_subdev_s_parm,
};

static const struct v4l2_subdev_pad_ops sunxi_csi_subdev_pad_ops = {
	.enum_mbus_code = sunxi_csi_enum_mbus_code,
	.get_fmt = sunxi_csi_subdev_get_fmt,
	.set_fmt = sunxi_csi_subdev_set_fmt,
};

static struct v4l2_subdev_ops sunxi_csi_subdev_ops = {
	.core = &sunxi_csi_core_ops,
	.video = &sunxi_csi_subdev_video_ops,
	.pad = &sunxi_csi_subdev_pad_ops,
};

static int __csi_init_subdev(struct csi_dev *csi)
{
	struct v4l2_subdev *sd = &csi->subdev;
	int ret;
	mutex_init(&csi->subdev_lock);
	csi->arrange.row = 1;
	csi->arrange.column = 1;
	csi->bus_info.ch_total_num = 1;
	v4l2_subdev_init(sd, &sunxi_csi_subdev_ops);
	sd->grp_id = VIN_GRP_ID_CSI;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "sunxi_csi.%u", csi->id);
	v4l2_set_subdevdata(sd, csi);

	csi->csi_pads[CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	csi->csi_pads[CSI_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;

	ret = media_entity_init(&sd->entity, CSI_PAD_NUM, csi->csi_pads, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int csi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct csi_dev *csi = NULL;
	int ret = 0;

	if (np == NULL) {
		vin_err("CSI failed to get of node\n");
		return -ENODEV;
	}
	csi = kzalloc(sizeof(struct csi_dev), GFP_KERNEL);
	if (!csi) {
		ret = -ENOMEM;
		goto ekzalloc;
	}

	of_property_read_u32(np, "device_id", &pdev->id);
	if (pdev->id < 0) {
		vin_err("CSI failed to get device id\n");
		ret = -EINVAL;
		goto freedev;
	}
	csi->id = pdev->id;
	csi->pdev = pdev;
	csi->cur_ch = 0;

	/*just for test because the csi1 is virtual node*/
	csi->base = of_iomap(np, 0);
	if (!csi->base) {
		ret = -EIO;
		goto freedev;
	}
#if defined(CONFIG_ARCH_SUN8IW11P1) || defined(CONFIG_ARCH_SUN50IW1P1)
	ret = bsp_csi_set_base_addr(csi->id, (unsigned long)csi->base);
#else
	ret = csic_prs_set_base_addr(csi->id, (unsigned long)csi->base);
#endif
	if (ret < 0)
		goto unmap;

	if (__csi_clk_get(csi)) {
		vin_err("csi clock get failed!\n");
		return -ENXIO;
	}

	spin_lock_init(&csi->slock);
	init_waitqueue_head(&csi->wait);
	list_add_tail(&csi->csi_list, &csi_drv_list);
	__csi_init_subdev(csi);

	platform_set_drvdata(pdev, csi);
	vin_print("csi%d probe end!\n", csi->id);

	return 0;

unmap:
	iounmap(csi->base);
freedev:
	kfree(csi);
ekzalloc:
	vin_print("csi probe err!\n");
	return ret;
}

static int csi_remove(struct platform_device *pdev)
{
	struct csi_dev *csi = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &csi->subdev;

	platform_set_drvdata(pdev, NULL);
	v4l2_set_subdevdata(sd, NULL);
	__csi_pin_release(csi);
	__csi_clk_release(csi);
	mutex_destroy(&csi->subdev_lock);
	if (csi->base)
		iounmap(csi->base);
	list_del(&csi->csi_list);
	kfree(csi);
	return 0;
}

static const struct of_device_id sunxi_csi_match[] = {
	{.compatible = "allwinner,sunxi-csi",},
	{},
};

static struct platform_driver csi_platform_driver = {
	.probe = csi_probe,
	.remove = csi_remove,
	.driver = {
		   .name = CSI_MODULE_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_csi_match,
		   }
};

void sunxi_csi_dump_regs(struct v4l2_subdev *sd)
{
	struct csi_dev *csi = v4l2_get_subdevdata(sd);
	int i = 0;
	printk("Vfe dump CSI regs :\n");
	for (i = 0; i < 0xb0; i = i + 4) {
		if (i % 0x10 == 0)
			printk("0x%08x:    ", i);
		printk("0x%08x, ", readl(csi->base + i));
		if (i % 0x10 == 0xc)
			printk("\n");
	}
}

struct v4l2_subdev *sunxi_csi_get_subdev(int id)
{
	struct csi_dev *csi;
	list_for_each_entry(csi, &csi_drv_list, csi_list) {
		if (csi->id == id) {
			csi->use_cnt++;
			return &csi->subdev;
		}
	}
	return NULL;
}

int sunxi_csi_platform_register(void)
{
	return platform_driver_register(&csi_platform_driver);
}

void sunxi_csi_platform_unregister(void)
{
	platform_driver_unregister(&csi_platform_driver);
	vin_print("csi_exit end\n");
}
