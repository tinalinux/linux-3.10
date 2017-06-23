
/*
 ******************************************************************************
 *
 * sunxi_isp.c
 *
 * Hawkview ISP - sunxi_isp.c module
 *
 * Copyright (c) 2014 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   3.0		  Yang Feng   	2014/12/11	ISP Tuning Tools Support
 *
 ******************************************************************************
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include "../platform/platform_cfg.h"
#include "sunxi_isp.h"
#include "../vin-video/vin_core.h"
#include "../utility/vin_io.h"

#define ISP_MODULE_NAME "vin_isp"

extern unsigned int isp_reparse_flag;

static LIST_HEAD(isp_drv_list);

#define MIN_IN_WIDTH			32
#define MIN_IN_HEIGHT			32
#define MAX_IN_WIDTH			4095
#define MAX_IN_HEIGHT			4095

static struct isp_pix_fmt sunxi_isp_formats[] = {
	{
		.name = "RAW8 (BGGR)",
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.depth = {8},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR8_1X8,
		.infmt = ISP_BGGR,
	}, {
		.name = "RAW8 (GBRG)",
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.depth = {8},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGBRG8_1X8,
		.infmt = ISP_GBRG,
	}, {
		.name = "RAW8 (GRBG)",
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.depth = {8},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGRBG8_1X8,
		.infmt = ISP_GRBG,
	}, {
		.name = "RAW8 (RGGB)",
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.depth = {8},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SRGGB8_1X8,
		.infmt = ISP_RGGB,
	}, {
		.name = "RAW10 (BGGR)",
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.depth = {10},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR10_1X10,
		.infmt = ISP_BGGR,
	}, {
		.name = "RAW10 (GBRG)",
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.depth = {10},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGBRG10_1X10,
		.infmt = ISP_GBRG,
	}, {
		.name = "RAW10 (GRBG)",
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.depth = {10},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGRBG10_1X10,
		.infmt = ISP_GRBG,
	}, {
		.name = "RAW10 (RGGB)",
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.depth = {10},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SRGGB10_1X10,
		.infmt = ISP_RGGB,
	}, {
		.name = "RAW12 (BGGR)",
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.depth = {12},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SBGGR12_1X12,
		.infmt = ISP_BGGR,
	}, {
		.name = "RAW12 (GBRG)",
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.depth = {12},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGBRG12_1X12,
		.infmt = ISP_GBRG,
	}, {
		.name = "RAW12 (GRBG)",
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.depth = {12},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SGRBG12_1X12,
		.infmt = ISP_GRBG,
	}, {
		.name = "RAW12 (RGGB)",
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.depth = {12},
		.color = 0,
		.memplanes = 1,
		.mbus_code = V4L2_MBUS_FMT_SRGGB12_1X12,
		.infmt = ISP_RGGB,
	},
};

static int isp_3d_pingpong_alloc(struct isp_dev *isp)
{
	int ret = 0;
	isp->isp_3d_buf.flags[0] = ISP_3D_R;
	isp->isp_3d_buf.flags[1] = ISP_3D_W;

	isp->isp_3d_buf.buf[0].size = isp->mf.width * isp->mf.height * 4;
	isp->isp_3d_buf.buf[1].size = isp->mf.width * isp->mf.height * 4;

	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_3d_buf.buf[0]);
	if (ret < 0) {
		vin_err("isp 3d pingpong buf0 requset add failed!\n");
		return -ENOMEM;
	}

	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_3d_buf.buf[1]);
	if (ret < 0) {
		vin_err("isp 3d pingpong buf1 requset add failed!\n");
		return -ENOMEM;
	}
	return ret;

}
static void isp_3d_pingpong_free(struct isp_dev *isp)
{
	os_mem_free(&isp->pdev->dev, &isp->isp_3d_buf.buf[0]);
	os_mem_free(&isp->pdev->dev, &isp->isp_3d_buf.buf[1]);
}

static int isp_3d_pingpong_update(struct isp_dev *isp,
				struct isp_hw_pingpong *pingpong)
{
	dma_addr_t addr;
	int tmp = -1;

	tmp = pingpong->flags[0];
	pingpong->flags[0] = pingpong->flags[1];
	pingpong->flags[1] = tmp;

	addr = (dma_addr_t)pingpong->buf[pingpong->flags[ISP_3D_W]].dma_addr;
	writel(addr >> 2, isp->isp_load.vir_addr + 0xc8);
	addr = (dma_addr_t)pingpong->buf[pingpong->flags[ISP_3D_R]].dma_addr;
	writel(addr >> 2, isp->isp_load.vir_addr + 0xcc);

	return 0;
}

static int sunxi_isp_subdev_set_crop(struct v4l2_subdev *sd,
				const struct v4l2_crop *crop)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	isp->isp_ob.ob_valid.width = crop->c.width;
	isp->isp_ob.ob_valid.height = crop->c.height;
	isp->isp_ob.ob_start.hor = crop->c.left;
	isp->isp_ob.ob_start.ver = crop->c.top;
	isp->isp_ob.ob_black.width = crop->c.width + crop->c.left;
	isp->isp_ob.ob_black.height = crop->c.height + crop->c.top;
	return 0;
}

static int sunxi_isp_subdev_s_power(struct v4l2_subdev *sd, int enable)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (!isp->use_isp)
		return 0;

	return 0;
}

static int sunxi_isp_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &isp->mf;
	struct mbus_framefmt_res *res = (void *)mf->reserved;
	struct isp_size_settings *ob = &isp->isp_ob;

	if (!isp->use_isp)
		return 0;

	switch (res->res_pix_fmt) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		vin_print("%s output fmt is raw, return directly\n", __func__);
		return 0;
	default:
		break;
	}

	if (enable) {
		isp->h3a_stat.frame_number = 0;

		if (isp_3d_pingpong_alloc(isp))
			return -ENOMEM;
		isp_3d_pingpong_update(isp, &isp->isp_3d_buf);

		isp->load_shadow = kzalloc(ISP_LOAD_REG_SIZE, GFP_KERNEL);
		if (isp->load_shadow == NULL) {
			vin_err("%s - kzalloc load_shadow failed\n", __func__);
			return -ENOMEM;
		}

		bsp_isp_enable(isp->id);

		bsp_isp_src0_enable(isp->id);
		bsp_isp_set_input_fmt(isp->id, isp->isp_fmt->infmt);
		ob->ob_valid.width = mf->width;
		ob->ob_valid.height = mf->height;
		ob->ob_start.hor = 0;
		ob->ob_start.ver = 0;
		ob->ob_black.width = mf->width + ob->ob_start.hor;
		ob->ob_black.height = mf->height + ob->ob_start.ver;
		bsp_isp_set_ob_zone(isp->id, &isp->isp_ob);
		sunxi_isp_dump_regs(sd);

		bsp_isp_set_para_ready(isp->id);

		bsp_isp_set_speed_mode(isp->id, 3);
		bsp_isp_channel_enable(isp->id, 0);
		bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
		bsp_isp_irq_enable(isp->id, FINISH_INT_EN | SRC0_FIFO_INT_EN
				     | FRAME_ERROR_INT_EN | FRAME_LOST_INT_EN);
		bsp_isp_capture_start(isp->id);
	} else {
		bsp_isp_capture_stop(isp->id);
		bsp_isp_src0_disable(isp->id);
		bsp_isp_channel_disable(isp->id, 0);
		bsp_isp_irq_disable(isp->id, ISP_IRQ_EN_ALL);
		bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
		kfree(isp->load_shadow);
		bsp_isp_disable(isp->id);
		isp_3d_pingpong_free(isp);
	}

	vin_print("%s on = %d, %d*%d %x %d\n", __func__, enable,
		mf->width, mf->height, mf->code, mf->field);

	return 0;
}

static struct isp_pix_fmt *__isp_find_format(const u32 *pixelformat,
							const u32 *mbus_code,
							int index)
{
	struct isp_pix_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(sunxi_isp_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(sunxi_isp_formats); ++i) {
		fmt = &sunxi_isp_formats[i];
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

static struct v4l2_mbus_framefmt *__isp_get_format(struct isp_dev *isp,
					struct v4l2_subdev_fh *fh,
					enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, 0) : NULL;
	return &isp->mf;
}

static struct isp_pix_fmt *__isp_try_format(struct v4l2_mbus_framefmt *mf)
{
	struct isp_pix_fmt *isp_fmt;

	isp_fmt = __isp_find_format(NULL, &mf->code, 0);
	if (isp_fmt == NULL)
		isp_fmt = &sunxi_isp_formats[0];

	mf->width = clamp_t(u32, mf->width, MIN_IN_WIDTH, MAX_IN_WIDTH);
	mf->height = clamp_t(u32, mf->height, MIN_IN_HEIGHT, MAX_IN_HEIGHT);

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	return isp_fmt;
}

static int sunxi_isp_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	isp->capture_mode = cp->capturemode;

	return 0;
}

static int sunxi_isp_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->capturemode = isp->capture_mode;

	return 0;
}

static int sunxi_isp_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	struct isp_pix_fmt *fmt;

	fmt = __isp_find_format(NULL, NULL, code->index);
	if (!fmt)
		return -EINVAL;
	code->code = fmt->mbus_code;
	return 0;
}

static int sunxi_isp_enum_frame_size(struct v4l2_subdev *sd,
				     struct v4l2_subdev_fh *fh,
				     struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index != 0)
		return -EINVAL;

	fse->min_width = MIN_IN_WIDTH;
	fse->min_height = MIN_IN_HEIGHT;
	fse->max_width = MAX_IN_WIDTH;
	fse->max_height = MAX_IN_HEIGHT;

	return 0;
}

static int sunxi_isp_subdev_get_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_format *fmt)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __isp_get_format(isp, fh, fmt->which);
	if (!mf)
		return -EINVAL;

	mutex_lock(&isp->subdev_lock);
	fmt->format = *mf;
	mutex_unlock(&isp->subdev_lock);
	return 0;
}

static int sunxi_isp_subdev_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_format *fmt)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	struct isp_pix_fmt *isp_fmt;

	vin_log(VIN_LOG_FMT, "%s %d*%d %x %d\n", __func__,
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field);

	mf = __isp_get_format(isp, fh, fmt->which);

	if (fmt->pad == ISP_PAD_SOURCE) {
		if (mf) {
			mutex_lock(&isp->subdev_lock);
			fmt->format = *mf;
			mutex_unlock(&isp->subdev_lock);
		}
		return 0;
	}
	isp_fmt = __isp_try_format(&fmt->format);
	if (mf) {
		mutex_lock(&isp->subdev_lock);
		*mf = fmt->format;
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			isp->isp_fmt = isp_fmt;
		mutex_unlock(&isp->subdev_lock);
	}

	return 0;

}

int sunxi_isp_subdev_init(struct v4l2_subdev *sd, u32 val)
{
	int ret = 0;
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (!isp->use_isp)
		return 0;

	vin_print("%s, val = %d.\n", __func__, val);
	if (val) {
		bsp_isp_set_dma_load_addr(isp->id, (unsigned long)isp->isp_load.dma_addr);
		bsp_isp_set_dma_saved_addr(isp->id, (unsigned long)isp->isp_save.dma_addr);
		ret = isp_table_request(sd);
		if (ret) {
			vin_err("isp_table_request error at %s\n", __func__);
			return ret;
		}
		INIT_WORK(&isp->isp_isr_bh_task, isp_isr_bh_handle);

		isp_table_setting(sd);
		bsp_isp_clr_para_ready(isp->id);
	} else {
		flush_work(&isp->isp_isr_bh_task);
		isp_table_release(sd);
	}
	return 0;
}

static int __isp_set_load_reg(struct v4l2_subdev *sd, struct isp_table_reg_map *reg)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	if (!isp->use_isp)
		return 0;
	isp->load_flag = 1;
	return copy_from_user(isp->load_shadow, reg->addr, reg->size);
}

static int __isp_set_table1_map(struct v4l2_subdev *sd, struct isp_table_reg_map *tbl)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	int ret;
	if (!isp->use_isp)
		return 0;
	ret = copy_from_user(isp->isp_lut_tbl.vir_addr, tbl->addr, tbl->size);
	if (ret < 0) {
		vin_err("copy table mapping1 from usr error!\n");
		return ret;
	}

	return 0;
}

static int __isp_set_table2_map(struct v4l2_subdev *sd, struct isp_table_reg_map *tbl)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	int ret;
	if (!isp->use_isp)
		return 0;
	ret = copy_from_user(isp->isp_drc_tbl.vir_addr, tbl->addr, tbl->size);
	if (ret < 0) {
		vin_err("copy table mapping2 from usr error!\n");
		return ret;
	}
	return 0;
}

static long sunxi_isp_subdev_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
				   void *arg)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
		ret = __isp_set_load_reg(sd, (struct isp_table_reg_map *)arg);
		break;
	case VIDIOC_VIN_ISP_TABLE1_MAP:
		ret = __isp_set_table1_map(sd, (struct isp_table_reg_map *)arg);
		break;
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		ret = __isp_set_table2_map(sd, (struct isp_table_reg_map *)arg);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT

struct isp_table_reg_map32 {
	compat_caddr_t addr;
	unsigned int size;
};

#define VIDIOC_VIN_ISP_LOAD_REG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 70, struct isp_table_reg_map32)

#define VIDIOC_VIN_ISP_TABLE1_MAP32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 71, struct isp_table_reg_map32)

#define VIDIOC_VIN_ISP_TABLE2_MAP32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 72, struct isp_table_reg_map32)

static int get_isp_table_reg_map32(struct isp_table_reg_map *kp,
			      struct isp_table_reg_map32 __user *up)
{
	u32 tmp;
	if (!access_ok(VERIFY_READ, up, sizeof(struct isp_table_reg_map32)) ||
	    get_user(kp->size, &up->size) || get_user(tmp, &up->addr))
		return -EFAULT;
	kp->addr = compat_ptr(tmp);
	return 0;
}

static int put_isp_table_reg_map32(struct isp_table_reg_map *kp,
			      struct isp_table_reg_map32 __user *up)
{
	u32 tmp = (u32) ((unsigned long)kp->addr);
	if (!access_ok(VERIFY_WRITE, up, sizeof(struct isp_table_reg_map32)) ||
	    put_user(kp->size, &up->size) || put_user(tmp, &up->addr))
		return -EFAULT;
	return 0;
}

static long isp_compat_ioctl32(struct v4l2_subdev *sd,
		unsigned int cmd, unsigned long arg)
{
	union {
		struct isp_table_reg_map isd;
	} karg;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;
	vin_log(VIN_LOG_ISP, "%s cmd is %d\n", __func__, cmd);

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG32:
		cmd = VIDIOC_VIN_ISP_LOAD_REG;
		break;
	case VIDIOC_VIN_ISP_TABLE1_MAP32:
		cmd = VIDIOC_VIN_ISP_TABLE1_MAP;
		break;
	case VIDIOC_VIN_ISP_TABLE2_MAP32:
		cmd = VIDIOC_VIN_ISP_TABLE2_MAP;
		break;
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
	case VIDIOC_VIN_ISP_TABLE1_MAP:
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		err = get_isp_table_reg_map32(&karg.isd, up);
		compatible_arg = 0;
		break;
	}

	if (err)
		return err;

	if (compatible_arg)
		err = sunxi_isp_subdev_ioctl(sd, cmd, up);
	else {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sunxi_isp_subdev_ioctl(sd, cmd, &karg);
		set_fs(old_fs);
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
	case VIDIOC_VIN_ISP_TABLE1_MAP:
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		err = put_isp_table_reg_map32(&karg.isd, up);
		break;
	}

	return err;
}
#endif

static void sunxi_isp_stat_parse(struct isp_dev *isp)
{
	void *va = NULL;
	if (NULL == isp->h3a_stat.active_buf) {
		vin_log(VIN_LOG_ISP, "stat active buf is NULL, please enable\n");
		return;
	}
	va = isp->h3a_stat.active_buf->virt_addr;
	isp->stat_buf->hist_buf = (void *) (va);
	isp->stat_buf->ae_buf =  (void *) (va + ISP_STAT_AE_MEM_OFS);
	isp->stat_buf->af_buf = (void *) (va + ISP_STAT_AF_MEM_OFS);
	isp->stat_buf->afs_buf = (void *) (va + ISP_STAT_AFS_MEM_OFS);
	isp->stat_buf->awb_buf = (void *) (va + ISP_STAT_AWB_MEM_OFS);
	isp->stat_buf->pltm_buf = (void *) (va + ISP_STAT_PLTM_LST_MEM_OFS);
}

void sunxi_isp_vsync_isr(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_event event;

	memset(&event, 0, sizeof(event));
	event.type = V4L2_EVENT_VSYNC;
	event.id = 0;
	v4l2_event_queue(isp->subdev.devnode, &event);
}

void sunxi_isp_frame_sync_isr(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_event event;

	vin_isp_stat_isr_frame_sync(&isp->h3a_stat);

	memset(&event, 0, sizeof(event));
	event.type = V4L2_EVENT_FRAME_SYNC;
	event.id = 0;
	v4l2_event_queue(isp->subdev.devnode, &event);

	isp->h3a_stat.frame_number++;

	vin_isp_stat_isr(&isp->h3a_stat);
	/* you should enable the isp stat buf first,
	** when you want to get the stat buf separate.
	** user can get stat buf from external ioctl.
	*/
	sunxi_isp_stat_parse(isp);
}

int sunxi_isp_subscribe_event(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	vin_log(VIN_LOG_ISP, "%s id = %d\n", __func__, sub->id);
	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	else
		return v4l2_event_subscribe(fh, sub, 1, NULL);
}

int sunxi_isp_unsubscribe_event(struct v4l2_subdev *sd,
				    struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	vin_log(VIN_LOG_ISP, "%s id = %d\n", __func__, sub->id);
	return v4l2_event_unsubscribe(fh, sub);
}

static const struct v4l2_subdev_core_ops sunxi_isp_subdev_core_ops = {
	.s_power = sunxi_isp_subdev_s_power,
	.init = sunxi_isp_subdev_init,
	.ioctl = sunxi_isp_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = isp_compat_ioctl32,
#endif
	.subscribe_event = sunxi_isp_subscribe_event,
	.unsubscribe_event = sunxi_isp_unsubscribe_event,
};

static const struct v4l2_subdev_video_ops sunxi_isp_subdev_video_ops = {
	.s_parm = sunxi_isp_s_parm,
	.g_parm = sunxi_isp_g_parm,
	.s_crop = sunxi_isp_subdev_set_crop,
	.s_stream = sunxi_isp_subdev_s_stream,
};

static const struct v4l2_subdev_pad_ops sunxi_isp_subdev_pad_ops = {
	.enum_mbus_code = sunxi_isp_enum_mbus_code,
	.enum_frame_size = sunxi_isp_enum_frame_size,
	.get_fmt = sunxi_isp_subdev_get_fmt,
	.set_fmt = sunxi_isp_subdev_set_fmt,
};

static struct v4l2_subdev_ops sunxi_isp_subdev_ops = {
	.core = &sunxi_isp_subdev_core_ops,
	.video = &sunxi_isp_subdev_video_ops,
	.pad = &sunxi_isp_subdev_pad_ops,
};

static int __sunxi_isp_ctrl(struct isp_dev *isp, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	case V4L2_CID_ROTATE:
		break;
	default:
		break;
	}
	return ret;
}

#define ctrl_to_sunxi_isp(ctrl) \
	container_of(ctrl->handler, struct isp_dev, ctrls.handler)

static int sunxi_isp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isp_dev *isp = ctrl_to_sunxi_isp(ctrl);
	unsigned long flags;
	int ret;

	vin_log(VIN_LOG_ISP, "id = %d, val = %d, cur.val = %d\n",
		  ctrl->id, ctrl->val, ctrl->cur.val);
	spin_lock_irqsave(&isp->slock, flags);
	ret = __sunxi_isp_ctrl(isp, ctrl);
	spin_unlock_irqrestore(&isp->slock, flags);

	return ret;
}

static const struct v4l2_ctrl_ops sunxi_isp_ctrl_ops = {
	.s_ctrl = sunxi_isp_s_ctrl,
};

static const struct v4l2_ctrl_config ae_win_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_Y2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}
};

static const struct v4l2_ctrl_config af_win_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_Y2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}
};

static const struct v4l2_ctrl_config wb_gain_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_R_GAIN,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 1024,
		.step = 1,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_GR_GAIN,
		.name = "GR GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 1024,
		.step = 1,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_GB_GAIN,
		.name = "GB GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 1024,
		.step = 1,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_B_GAIN,
		.name = "B GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 1024,
		.step = 1,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}
};

int __isp_init_subdev(struct isp_dev *isp)
{
	const struct v4l2_ctrl_ops *ops = &sunxi_isp_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &isp->ctrls.handler;
	struct v4l2_subdev *sd = &isp->subdev;
	struct sunxi_isp_ctrls *ctrls = &isp->ctrls;
	int i, ret;
	mutex_init(&isp->subdev_lock);

	v4l2_subdev_init(sd, &sunxi_isp_subdev_ops);
	sd->grp_id = VIN_GRP_ID_ISP;
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "sunxi_isp.%u", isp->id);
	v4l2_set_subdevdata(sd, isp);

	v4l2_ctrl_handler_init(handler, 3 + ARRAY_SIZE(ae_win_ctrls)
		+ ARRAY_SIZE(af_win_ctrls) + ARRAY_SIZE(wb_gain_ctrls));

	ctrls->rotate =
	    v4l2_ctrl_new_std(handler, ops, V4L2_CID_ROTATE, 0, 270, 90, 0);
	ctrls->hflip =
	    v4l2_ctrl_new_std(handler, ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrls->vflip =
	    v4l2_ctrl_new_std(handler, ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	for (i = 0; i < ARRAY_SIZE(wb_gain_ctrls); i++)
		ctrls->wb_gain[i] = v4l2_ctrl_new_custom(handler,
						&wb_gain_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(wb_gain_ctrls), &ctrls->wb_gain[0]);

	for (i = 0; i < ARRAY_SIZE(ae_win_ctrls); i++)
		ctrls->ae_win[i] = v4l2_ctrl_new_custom(handler,
						&ae_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(ae_win_ctrls), &ctrls->ae_win[0]);

	for (i = 0; i < ARRAY_SIZE(af_win_ctrls); i++)
		ctrls->af_win[i] = v4l2_ctrl_new_custom(handler,
						&af_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(af_win_ctrls), &ctrls->af_win[0]);

	if (handler->error) {
		return handler->error;
	}

	/*sd->entity->ops = &isp_media_ops;*/
	isp->isp_pads[ISP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isp->isp_pads[ISP_PAD_SOURCE_ST].flags = MEDIA_PAD_FL_SOURCE;
	isp->isp_pads[ISP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;

	ret = media_entity_init(&sd->entity, ISP_PAD_NUM, isp->isp_pads, 0);
	if (ret < 0)
		return ret;

	sd->ctrl_handler = handler;
	/*sd->internal_ops = &sunxi_isp_sd_internal_ops;*/
	return 0;
}

void isp_table_setting(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	printk("isp->use_cnt = %d\n", isp->use_cnt);
	if (isp->use_cnt > 1)
		return;

	if (isp->is_raw && isp->use_isp) {
		bsp_isp_update_lens_gamma_table(isp->id, &isp->isp_tbl);
		bsp_isp_update_drc_table(isp->id, &isp->isp_tbl);
	}
}

int isp_table_request(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct isp_table_addr *tbl = &isp->isp_tbl;
	void *pa, *va, *dma;
	int ret;

	if (isp->use_cnt > 1)
		return 0;

	/*requeset for isp table and statistic buffer*/
	if (isp->use_isp && isp->is_raw) {
		isp->isp_lut_tbl.size = ISP_TABLE_MAPPING1_SIZE;
		ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_lut_tbl);
		if (ret < 0) {
			vin_err("isp lookup table request failed!\n");
			return -ENOMEM;
		}
		pa = isp->isp_lut_tbl.phy_addr;
		va = isp->isp_lut_tbl.vir_addr;
		dma = isp->isp_lut_tbl.dma_addr;
		tbl->isp_lsc_tbl_paddr = (void *)(pa + ISP_LSC_MEM_OFS);
		tbl->isp_lsc_tbl_dma_addr = (void *)(dma + ISP_LSC_MEM_OFS);
		tbl->isp_lsc_tbl_vaddr = (void *)(va + ISP_LSC_MEM_OFS);
		tbl->isp_gamma_tbl_paddr = (void *)(pa + ISP_GAMMA_MEM_OFS);
		tbl->isp_gamma_tbl_dma_addr = (void *)(dma + ISP_GAMMA_MEM_OFS);
		tbl->isp_gamma_tbl_vaddr = (void *)(va + ISP_GAMMA_MEM_OFS);
		tbl->isp_linear_tbl_paddr = (void *)(pa + ISP_LINEAR_MEM_OFS);
		tbl->isp_linear_tbl_dma_addr = (void *)(dma + ISP_LINEAR_MEM_OFS);
		tbl->isp_linear_tbl_vaddr = (void *)(va + ISP_LINEAR_MEM_OFS);

		vin_log(VIN_LOG_ISP, "isp_lsc_tbl_vaddr = %p\n",
			tbl->isp_lsc_tbl_vaddr);
		vin_log(VIN_LOG_ISP, "isp_gamma_tbl_vaddr = %p\n",
			tbl->isp_gamma_tbl_vaddr);

		isp->isp_drc_tbl.size = ISP_TABLE_MAPPING2_SIZE;
		ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_drc_tbl);
		if (ret < 0) {
			vin_err("isp drc table request pa failed!\n");
			return -ENOMEM;
		}
		pa = isp->isp_drc_tbl.phy_addr;
		va = isp->isp_drc_tbl.vir_addr;
		dma = isp->isp_drc_tbl.dma_addr;

		tbl->isp_drc_tbl_paddr = (void *)(pa + ISP_DRC_MEM_OFS);
		tbl->isp_drc_tbl_dma_addr = (void *)(dma + ISP_DRC_MEM_OFS);
		tbl->isp_drc_tbl_vaddr = (void *)(va + ISP_DRC_MEM_OFS);

		vin_log(VIN_LOG_ISP, "isp_drc_tbl_vaddr = %p\n",
				tbl->isp_drc_tbl_vaddr);
	}

	return 0;
}

/*static void isp_table_release(struct isp_dev *isp)*/
void isp_table_release(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (isp->use_cnt == 0)
		return;

	/* release isp table and statistic buffer */
	if (isp->use_isp && isp->is_raw) {
		os_mem_free(&isp->pdev->dev, &isp->isp_lut_tbl);
		os_mem_free(&isp->pdev->dev, &isp->isp_drc_tbl);
	}
}

static int isp_resource_alloc(struct isp_dev *isp)
{
	int ret = 0;
	isp->isp_load.size = ISP_LOAD_REG_SIZE;
	isp->isp_save.size = ISP_SAVED_REG_SIZE;

	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_load);
	if (ret < 0) {
		vin_err("isp load regs requset add failed!\n");
		return -ENOMEM;
	}
	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_save);
	if (ret < 0) {
		vin_err("isp save regs requset add failed!\n");
		return -ENOMEM;
	}
	return ret;

}
static void isp_resource_free(struct isp_dev *isp)
{
	os_mem_free(&isp->pdev->dev, &isp->isp_load);
	os_mem_free(&isp->pdev->dev, &isp->isp_save);
}

#define ISP_REGS(n) (void __iomem *)(ISP_REGS_BASE + n)

void isp_isr_bh_handle(struct work_struct *work)
{
	struct isp_dev *isp = container_of(work, struct isp_dev, isp_isr_bh_task);

	if (isp->is_raw) {
		mutex_lock(&isp->subdev_lock);
		if (1 == isp_reparse_flag) {
			vin_print("ISP reparse ini file!\n");
			isp_reparse_flag = 0;
		} else if (2 == isp_reparse_flag) {
			vin_reg_set(ISP_REGS(0x10), (1 << 20));
		} else if (3 == isp_reparse_flag) {
			vin_reg_clr_set(ISP_REGS(0x10), (0xF << 16), (1 << 16));
			vin_reg_set(ISP_REGS(0x10), (1 << 20));
		} else if (4 == isp_reparse_flag) {
			vin_reg_clr(ISP_REGS(0x10), (1 << 20));
			vin_reg_clr(ISP_REGS(0x10), (0xF << 16));
		}
		mutex_unlock(&isp->subdev_lock);
	}

}

static irqreturn_t isp_isr(int irq, void *priv)
{
	struct isp_dev *isp = (struct isp_dev *)priv;
	unsigned int update_flag;
	unsigned long flags;

	if (!isp->use_isp)
		return 0;

	vin_log(VIN_LOG_ISP, "isp %d interrupt, status is 0x%x!!!\n", isp->id,
		bsp_isp_get_irq_status(isp->id, ISP_IRQ_EN_ALL));

	spin_lock_irqsave(&isp->slock, flags);

	if (bsp_isp_get_irq_status(isp->id, SRC0_FIFO_INT_EN)) {
		vin_err("isp source0 fifo overflow\n");
		bsp_isp_clr_irq_status(isp->id, SRC0_FIFO_INT_EN);
		goto unlock;
	}

	if (bsp_isp_get_irq_status(isp->id, FRAME_ERROR_INT_EN)) {
		vin_err("isp frame error\n");
		bsp_isp_clr_irq_status(isp->id, FRAME_ERROR_INT_EN);
		goto unlock;
	}

	if (bsp_isp_get_irq_status(isp->id, FRAME_LOST_INT_EN)) {
		vin_err("isp frame lost\n");
		bsp_isp_clr_irq_status(isp->id, FRAME_LOST_INT_EN);
		goto unlock;
	}

unlock:
	spin_unlock_irqrestore(&isp->slock, flags);
	if (bsp_isp_get_irq_status(isp->id, FINISH_INT_EN)) {
		bsp_isp_irq_disable(isp->id, FINISH_INT_EN);
		vin_log(VIN_LOG_ISP, "call tasklet schedule!\n");
		bsp_isp_clr_para_ready(isp->id);
		schedule_work(&isp->isp_isr_bh_task);
		if (isp->load_flag) {
			memcpy(isp->isp_load.vir_addr, isp->load_shadow, 0x400);
			isp->load_flag = 0;
		}
		bsp_isp_set_ob_zone(isp->id, &isp->isp_ob);
		update_flag = readl(isp->isp_load.vir_addr + 0x04);
		bsp_isp_update_table(isp->id, (unsigned short)update_flag);
		isp_3d_pingpong_update(isp, &isp->isp_3d_buf);
		bsp_isp_set_para_ready(isp->id);
		sunxi_isp_frame_sync_isr(&isp->subdev);
	}

	bsp_isp_clr_irq_status(isp->id, FINISH_INT_EN);
	bsp_isp_irq_enable(isp->id, FINISH_INT_EN);

	return IRQ_HANDLED;
}

unsigned int isp_default_reg[0x100] = {
	0x00000101, 0x00000001, 0x00004111, 0x00000087,
	0x03c00010, 0x00000000, 0x28000000, 0x04000000,
	0x0dc11000, 0x0dc11400, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x80000000, 0x00000004, 0x00000000, 0x0136003c,
	0x00000106, 0x00005040, 0x00000000, 0x00000000,
	0x00000000, 0x000f0013, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x01e00280, 0x01e00280,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x0f000200, 0x01390010,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x04000804, 0x00000000, 0x00000000, 0x00000000,
	0x00021010, 0x00000000, 0x00000000, 0x00000000,
	0x00400010, 0x01000100, 0x00200020, 0x00000100,
	0x00200020, 0x00200020, 0x04000400, 0x04000400,
	0x00200020, 0x00200020, 0x00ff00ff, 0x000000ff,
	0x000f0013, 0x00000000, 0x00000000, 0x00000000,
	0x00080008, 0x00000000, 0x00000000, 0x00000000,
	0x40070f01, 0xfcff0080, 0x1f173c2d, 0x001845c8,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x01000100, 0x01000100, 0x00000fff, 0x00000010,
	0x30000000, 0x00000080, 0x0003875c, 0x00400010,
	0x02000200, 0x04000400, 0x00000000, 0x00000484,
	0x00000808, 0x00420077, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x02040107, 0x07680064, 0x01c206d6, 0x068701c2,
	0x000007b7, 0x02010010, 0x00000008, 0x00000000,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00000000, 0x00000000, 0x00000000,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x02108421, 0x02108421,
	0x02108421, 0x02108421, 0x02108421, 0x02108421,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00000000, 0x00000000,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00080008, 0x00080008,
	0x00080008, 0x00080008, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static int isp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct isp_dev *isp = NULL;
	int ret = 0;

	if (np == NULL) {
		vin_err("ISP failed to get of node\n");
		return -ENODEV;
	}

	isp = kzalloc(sizeof(struct isp_dev), GFP_KERNEL);
	if (!isp) {
		ret = -ENOMEM;
		goto ekzalloc;
	}

	isp->stat_buf = kzalloc(sizeof(struct isp_stat_to_user), GFP_KERNEL);
	if (!isp->stat_buf) {
		vin_err("request stat_buf struct failed!\n");
		return -ENOMEM;
	}

	of_property_read_u32(np, "device_id", &pdev->id);
	if (pdev->id < 0) {
		vin_err("ISP failed to get device id\n");
		ret = -EINVAL;
		goto freedev;
	}

	isp->id = pdev->id;
	isp->pdev = pdev;

	isp->base = of_iomap(np, 0);
	if (!isp->base) {
		isp->is_empty = 1;
		isp->base = kzalloc(0x300, GFP_KERNEL);
		if (!isp->base) {
			ret = -EIO;
			goto freedev;
		}
	} else {
		isp->is_empty = 0;
		/*get irq resource */
		isp->irq = irq_of_parse_and_map(np, 0);
		if (isp->irq <= 0) {
			vin_err("failed to get ISP IRQ resource\n");
			goto unmap;
		}

		ret = request_irq(isp->irq, isp_isr, IRQF_DISABLED, isp->pdev->name, isp);
		if (ret) {
			vin_err("isp%d request irq failed\n", isp->id);
			goto unmap;
		}
	}

	list_add_tail(&isp->isp_list, &isp_drv_list);
	__isp_init_subdev(isp);

	spin_lock_init(&isp->slock);

	if (isp_resource_alloc(isp) < 0) {
		ret = -ENOMEM;
		goto freeirq;
	}

	ret = vin_isp_h3a_init(isp);
	if (ret < 0) {
		vin_err("VIN H3A initialization failed\n");
			goto free_res;
	}

	bsp_isp_init_platform(SUNXI_PLATFORM_ID);
	bsp_isp_set_base_addr(isp->id, (unsigned long)isp->base);
	bsp_isp_set_map_load_addr(isp->id, (unsigned long)isp->isp_load.vir_addr);
	bsp_isp_set_map_saved_addr(isp->id, (unsigned long)isp->isp_save.vir_addr);
	memset(isp->isp_load.vir_addr, 0, ISP_LOAD_REG_SIZE);
	memset(isp->isp_save.vir_addr, 0, ISP_SAVED_REG_SIZE);
	memcpy(isp->isp_load.vir_addr, &isp_default_reg[0], 0x400);
	platform_set_drvdata(pdev, isp);
	vin_print("isp%d probe end!\n", isp->id);
	return 0;
free_res:
	isp_resource_free(isp);
freeirq:
	if (!isp->is_empty)
		free_irq(isp->irq, isp);
unmap:
	if (!isp->is_empty)
		iounmap(isp->base);
	else
		kfree(isp->base);
	list_del(&isp->isp_list);
freeirq:
#ifdef DEFINE_ISP_REGS
	free_irq(isp->irq, isp);
#endif
freedev:
	kfree(isp);
ekzalloc:
	vin_print("isp probe err!\n");
	return ret;
}

static int isp_remove(struct platform_device *pdev)
{
	struct isp_dev *isp = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &isp->subdev;

	platform_set_drvdata(pdev, NULL);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	v4l2_set_subdevdata(sd, NULL);

	isp_resource_free(isp);
	if (!isp->is_empty) {
		free_irq(isp->irq, isp);
		if (isp->base)
			iounmap(isp->base);
	} else {
		kfree(isp->base);
	}
	list_del(&isp->isp_list);
	kfree(isp->stat_buf);
	vin_isp_h3a_cleanup(isp);
	kfree(isp);
	return 0;
}

static const struct of_device_id sunxi_isp_match[] = {
	{.compatible = "allwinner,sunxi-isp",},
	{},
};

static struct platform_driver isp_platform_driver = {
	.probe = isp_probe,
	.remove = isp_remove,
	.driver = {
		   .name = ISP_MODULE_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_isp_match,
		   }
};

void sunxi_isp_sensor_type(struct v4l2_subdev *sd, int use_isp, int is_raw)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	isp->use_isp = use_isp;
	isp->is_raw = is_raw;
}

void sunxi_isp_dump_regs(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	int i = 0;
	printk("vin dump ISP regs :\n");
	for (i = 0; i < 0x40; i = i + 4) {
		if (i % 0x10 == 0)
			printk("0x%08x:  ", i);
		printk("0x%08x, ", readl(isp->base + i));
		if (i % 0x10 == 0xc)
			printk("\n");
	}
	for (i = 0x40; i < 0x240; i = i + 4) {
		if (i % 0x10 == 0)
			printk("0x%08x:  ", i);
		printk("0x%08x, ", readl(isp->isp_load.vir_addr + i));
		if (i % 0x10 == 0xc)
			printk("\n");
	}
}

struct v4l2_subdev *sunxi_isp_get_subdev(int id)
{
	struct isp_dev *isp;
	list_for_each_entry(isp, &isp_drv_list, isp_list) {
		if (isp->id == id) {
			isp->use_cnt++;
			return &isp->subdev;
		}
	}
	return NULL;
}

struct v4l2_subdev *sunxi_stat_get_subdev(int id)
{
	struct isp_dev *isp;
	list_for_each_entry(isp, &isp_drv_list, isp_list) {
		if (isp->id == id) {
			return &isp->h3a_stat.sd;
		}
	}
	return NULL;
}

int sunxi_isp_platform_register(void)
{
	return platform_driver_register(&isp_platform_driver);
}

void sunxi_isp_platform_unregister(void)
{
	platform_driver_unregister(&isp_platform_driver);
	vin_print("isp_exit end\n");
}
