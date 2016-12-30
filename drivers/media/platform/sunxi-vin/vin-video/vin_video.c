
/*
 ******************************************************************************
 *
 * vin_video.c
 *
 * Hawkview ISP - vin_video.c module
 *
 * Copyright (c) 2015 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   3.0		  Zhao Wei   	2015/11/30	ISP Tuning Tools Support
 *
 ******************************************************************************
 */

#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/sort.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/moduleparam.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include <linux/regulator/consumer.h>
#ifdef CONFIG_DEVFREQ_DRAM_FREQ_WITH_SOFT_NOTIFY
#include <linux/sunxi_dramfreq.h>
#endif

#include "../utility/config.h"
#include "../utility/sensor_info.h"
#include "../utility/vin_io.h"
#include "../vin-csi/sunxi_csi.h"
#include "../vin-isp/sunxi_isp.h"
#include "../vin-vipp/sunxi_scaler.h"
#include "../vin-mipi/sunxi_mipi.h"
#include "../vin.h"

#define VIN_MAJOR_VERSION 1
#define VIN_MINOR_VERSION 0
#define VIN_RELEASE       0

#define VIN_VERSION \
  KERNEL_VERSION(VIN_MAJOR_VERSION, VIN_MINOR_VERSION, VIN_RELEASE)

void __vin_s_stream_handle(struct work_struct *work)
{
	int ret = 0;
	struct vin_vid_cap *cap =
			container_of(work, struct vin_vid_cap, s_stream_task);
	ret = vin_pipeline_call(cap->vinc, set_stream, &cap->pipe, 1);
	if (ret < 0) {
		vin_err("%s error!\n", __func__);
	}
	vin_print("%s done, id = %d!\n", __func__, cap->vinc->id);
}

/* The color format (colplanes, memplanes) must be already configured. */
int vin_set_addr(struct vin_core *vinc, struct vb2_buffer *vb,
		      struct vin_frame *frame, struct vin_addr *paddr)
{
	u32 pix_size, depth;

	if (vb == NULL || frame == NULL)
		return -EINVAL;

	pix_size = ALIGN(frame->o_width, VIN_ALIGN_WIDTH) * frame->o_height;
	depth = frame->fmt->depth[0] + frame->fmt->depth[1] + frame->fmt->depth[2];

	vin_log(VIN_LOG_VIDEO, "memplanes= %d, colplanes= %d, pix_size= %d\n",
		frame->fmt->memplanes, frame->fmt->colplanes, pix_size);

	paddr->y = vb2_dma_contig_plane_dma_addr(vb, 0);

	if (frame->fmt->memplanes == 1) {
		switch (frame->fmt->colplanes) {
		case 1:
			paddr->cb = 0;
			paddr->cr = 0;
			break;
		case 2:
			/* decompose Y into Y/Cb */
			paddr->cb = (u32)(paddr->y + pix_size);
			paddr->cr = 0;
			break;
		case 3:
			paddr->cb = (u32)(paddr->y + pix_size);
			/* 420 */
			if (12 == frame->fmt->depth[0])
				paddr->cr = (u32)(paddr->cb + (pix_size >> 2));
			else /* 422 */
				paddr->cr = (u32)(paddr->cb + (pix_size >> 1));
			break;
		default:
			return -EINVAL;
		}
	} else if (!frame->fmt->mdataplanes) {
		if (frame->fmt->memplanes >= 2)
			paddr->cb = vb2_dma_contig_plane_dma_addr(vb, 1);

		if (frame->fmt->memplanes == 3)
			paddr->cr = vb2_dma_contig_plane_dma_addr(vb, 2);
	}

	vin_log(VIN_LOG_VIDEO, "PHYS_ADDR: y = 0x%08x  cb = 0x%08x cr = 0x%08x",
			paddr->y, paddr->cb, paddr->cr);
#if defined (CONFIG_ARCH_SUN8IW11P1) || defined (CONFIG_ARCH_SUN50IW1P1)
	bsp_csi_set_addr(vinc->csi_sel, paddr->y, paddr->y, paddr->cb,
			paddr->cr);
#else

	if (vinc->vflip == 1) {
		paddr->y += pix_size - frame->o_width;
		switch (frame->fmt->colplanes) {
		case 1:
			paddr->cb = 0;
			paddr->cr = 0;
			break;
		case 2:
			/* 420 */
			if (12 == depth)
				paddr->cb += pix_size / 2 - frame->o_width;
			else /* 422 */
				paddr->cb += pix_size - frame->o_width;
			paddr->cr = 0;
			break;
		case 3:
			if (12 == depth) {
				paddr->cb += pix_size / 4 - frame->o_width / 2;
				paddr->cr += pix_size / 4 - frame->o_width / 2;
			} else {
				paddr->cb += pix_size / 2 - frame->o_width / 2;
				paddr->cr += pix_size / 2 - frame->o_width / 2;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	csic_dma_buffer_address(vinc->vipp_sel, CSI_BUF_0_A, paddr->y);
	csic_dma_buffer_address(vinc->vipp_sel, CSI_BUF_1_A, paddr->cb);
	csic_dma_buffer_address(vinc->vipp_sel, CSI_BUF_2_A, paddr->cr);
#endif
	return 0;
}

/*
 * Videobuf operations
 */
static int queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], void *alloc_ctxs[])
{
	struct vin_vid_cap *cap = vb2_get_drv_priv(vq);
	unsigned int size;
	int buf_max_flag = 0;
	int i;
	vin_log(VIN_LOG_VIDEO, "queue_setup\n");

	size = cap->frame.o_width * cap->frame.o_height;
	cap->frame.payload[0] = size * cap->frame.fmt->depth[0] / 8;
	cap->frame.payload[1] = size * cap->frame.fmt->depth[1] / 8;
	cap->frame.payload[2] = size * cap->frame.fmt->depth[2] / 8;
	cap->buf_byte_size =
		PAGE_ALIGN(cap->frame.payload[0]) +
		PAGE_ALIGN(cap->frame.payload[1]) +
		PAGE_ALIGN(cap->frame.payload[2]);

	size = cap->buf_byte_size;

	if (size == 0)
		return -EINVAL;

	if (0 == *nbuffers)
		*nbuffers = 8;

	while (size * *nbuffers > MAX_FRAME_MEM) {
		(*nbuffers)--;
		buf_max_flag = 1;
		if (*nbuffers == 0)
			vin_err("Buffer size > max frame memory! count = %d\n",
			     *nbuffers);
	}

	if (buf_max_flag == 0) {
		if (cap->capture_mode == V4L2_MODE_IMAGE) {
			if (*nbuffers != 1) {
				*nbuffers = 1;
				vin_err("buffer count != 1 in capture mode\n");
			}
		} else {
			if (*nbuffers < 3) {
				*nbuffers = 3;
				vin_err("buffer count is invalid, set to 3\n");
			}
		}
	}

	*nplanes = cap->frame.fmt->memplanes;
	for (i = 0; i < *nplanes; i++) {
		sizes[i] = cap->frame.payload[i];
		alloc_ctxs[i] = cap->alloc_ctx;
	}
	vin_print("%s, buf count = %d, nplanes = %d, size = %d\n",
		__func__, *nbuffers, *nplanes, size);
	cap->vinc->vin_status.buf_cnt = *nbuffers;
	cap->vinc->vin_status.buf_size = size;
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vin_vid_cap *cap = vb2_get_drv_priv(vb->vb2_queue);
	struct vin_buffer *buf = container_of(vb, struct vin_buffer, vb);
	/*unsigned long size;*/
	int i;
	vin_log(VIN_LOG_VIDEO, "buffer_prepare\n");

	if (cap->frame.o_width < MIN_WIDTH || cap->frame.o_width > MAX_WIDTH ||
	    cap->frame.o_height < MIN_HEIGHT || cap->frame.o_height > MAX_HEIGHT) {
		return -EINVAL;
	}
	/*size = dev->buf_byte_size;*/

	for (i = 0; i < cap->frame.fmt->memplanes; i++) {
		if (vb2_plane_size(vb, i) < cap->frame.payload[i]) {
			vin_err("%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, i),
				cap->frame.payload[i]);
			return -EINVAL;
		}
		vb2_set_plane_payload(&buf->vb, i, cap->frame.payload[i]);
		vb->v4l2_planes[i].m.mem_offset = vb2_dma_contig_plane_dma_addr(vb, i);
	}

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct vin_vid_cap *cap = vb2_get_drv_priv(vb->vb2_queue);
	struct vin_buffer *buf = container_of(vb, struct vin_buffer, vb);
	unsigned long flags = 0;

	vin_log(VIN_LOG_VIDEO, "buffer_queue\n");
	spin_lock_irqsave(&cap->slock, flags);
	list_add_tail(&buf->list, &cap->vidq_active);
	spin_unlock_irqrestore(&cap->slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vin_vid_cap *cap = vb2_get_drv_priv(vq);
	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);
	set_bit(VIN_STREAM, &cap->state);
	return 0;
}

/* abort streaming and wait for last buffer */
static int stop_streaming(struct vb2_queue *vq)
{
	struct vin_vid_cap *cap = vb2_get_drv_priv(vq);
	unsigned long flags = 0;

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);

	clear_bit(VIN_STREAM, &cap->state);

	spin_lock_irqsave(&cap->slock, flags);
	/* Release all active buffers */
	while (!list_empty(&cap->vidq_active)) {
		struct vin_buffer *buf;
		buf =
		    list_entry(cap->vidq_active.next, struct vin_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		vin_log(VIN_LOG_VIDEO, "buf %d stop\n", buf->vb.v4l2_buf.index);
	}
	spin_unlock_irqrestore(&cap->slock, flags);
	return 0;
}

static const struct vb2_ops vin_video_qops = {
	.queue_setup = queue_setup,
	.buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/*
 * IOCTL vidioc handling
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strcpy(cap->driver, "sunxi-vin");
	strcpy(cap->card, "sunxi-vin");

	cap->version = VIN_VERSION;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
	    V4L2_CAP_READWRITE;
	return 0;
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct vin_fmt *fmt;

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);
	fmt = vin_find_format(NULL, NULL, VIN_FMT_ALL, f->index, false);
	if (!fmt)
		return -EINVAL;

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	struct vin_core *vinc = video_drvdata(file);

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);

	if (vinc == NULL) {
		return -EINVAL;
	}

	return v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SENSOR], video,
				enum_framesizes, fsize);
}

static int vidioc_g_fmt_vid_cap_mplane(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct vin_vid_cap *cap =
	    container_of(video_devdata(file), struct vin_vid_cap, vdev);

	vin_get_fmt_mplane(&cap->frame, f);
	return 0;
}

static int vin_pipeline_try_format(struct vin_core *vinc,
				    struct v4l2_mbus_framefmt *tfmt,
				    struct vin_fmt **fmt_id,
				    bool set)
{
	struct v4l2_subdev *sd = vinc->vid_cap.pipe.sd[VIN_IND_SENSOR];
	struct v4l2_subdev_format sfmt;
	struct media_entity *me;
	struct vin_fmt *ffmt;
	unsigned int mask = (*fmt_id)->flags;
	struct media_entity_graph graph;
	int ret, i = 0, sd_ind;

	if (WARN_ON(!sd || !tfmt))
		return -EINVAL;

	memset(&sfmt, 0, sizeof(sfmt));
	sfmt.format = *tfmt;
	sfmt.which = set ? V4L2_SUBDEV_FORMAT_ACTIVE : V4L2_SUBDEV_FORMAT_TRY;

	if ((mask & VIN_FMT_YUV) && (vinc->support_raw == 0)) {
		mask = VIN_FMT_YUV;
	}

	/* when diffrent video output have same sensor,
	 * this pipeline try fmt will lead to false result.
	 * so it should be updated at later.
	 */
	while (1) {

		ffmt = vin_find_format(NULL, sfmt.format.code != 0 ? &sfmt.format.code : NULL,
					mask, i++, true);
		if (ffmt == NULL) {
			/*
			 * Notify user-space if common pixel code for
			 * host and sensor does not exist.
			 */
			vin_err("vin is not support this pixelformat\n");
			return -EINVAL;
		}

		sfmt.format.code = tfmt->code = ffmt->mbus_code;
		me = &vinc->vid_cap.subdev.entity;
		media_entity_graph_walk_start(&graph, me);
		while ((me = media_entity_graph_walk_next(&graph)) &&
			me != &vinc->vid_cap.subdev.entity) {

			sd = media_entity_to_v4l2_subdev(me);
			switch (sd->grp_id) {
			case VIN_GRP_ID_SENSOR:
				sd_ind = VIN_IND_SENSOR;
				break;
			case VIN_GRP_ID_MIPI:
				sd_ind = VIN_IND_MIPI;
				break;
			case VIN_GRP_ID_CSI:
				sd_ind = VIN_IND_CSI;
				break;
			case VIN_GRP_ID_ISP:
				sd_ind = VIN_IND_ISP;
				break;
			case VIN_GRP_ID_SCALER:
				sd_ind = VIN_IND_SCALER;
				break;
			case VIN_GRP_ID_CAPTURE:
				sd_ind = VIN_IND_CAPTURE;
				break;
			default:
				sd_ind = VIN_IND_SENSOR;
				break;
			}

			if (sd != vinc->vid_cap.pipe.sd[sd_ind])
				continue;
			vin_log(VIN_LOG_FMT, "found %s in this pipeline\n", me->name);

			if (me->num_pads == 1 &&
				(me->pads[0].flags & MEDIA_PAD_FL_SINK)) {
				vin_log(VIN_LOG_FMT, "skip %s.\n", me->name);
				continue;
			}

			sfmt.pad = 0;
			ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &sfmt);
			if (ret)
				return ret;

			/*change output resolution of scaler*/
			if (sd->grp_id == VIN_GRP_ID_SCALER) {
				sfmt.format.width = tfmt->width;
				sfmt.format.height = tfmt->height;
			}

			if (me->pads[0].flags & MEDIA_PAD_FL_SINK) {
				sfmt.pad = me->num_pads - 1;
				ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &sfmt);
				if (ret)
					return ret;
			}
		}

		if (sfmt.format.code != tfmt->code)
			continue;

		if (ffmt && ffmt->mbus_code)
			sfmt.format.code = ffmt->mbus_code;

		break;
	}

	if (fmt_id && ffmt)
		*fmt_id = ffmt;
	*tfmt = sfmt.format;

	return 0;
}

static int vin_pipeline_set_mbus_config(struct vin_core *vinc)
{
	struct vin_pipeline *pipe = &vinc->vid_cap.pipe;
	struct v4l2_subdev *sd = pipe->sd[VIN_IND_SENSOR];
	struct v4l2_mbus_config mcfg;
	struct media_entity *me;
	struct media_entity_graph graph;
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &mcfg);
	if (ret < 0) {
		vin_err("%s g_mbus_config error!\n", sd->name);
		goto out;
	}
	/* s_mbus_config on all mipi and csi */
	me = &vinc->vid_cap.subdev.entity;
	media_entity_graph_walk_start(&graph, me);
	while ((me = media_entity_graph_walk_next(&graph)) &&
		me != &vinc->vid_cap.subdev.entity) {
		sd = media_entity_to_v4l2_subdev(me);
		vin_log(VIN_LOG_VIDEO, "media entity is %s\n", me->name);
		if ((sd == pipe->sd[VIN_IND_MIPI]) ||
		    (sd == pipe->sd[VIN_IND_CSI])) {
			ret = v4l2_subdev_call(sd, video, s_mbus_config, &mcfg);
			if (ret < 0) {
				vin_err("%s s_mbus_config error!\n", me->name);
				goto out;
			}
		}
	}

	vinc->mbus_type = mcfg.type;
	return 0;
out:
	return ret;

}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	struct vin_core *vinc = video_drvdata(file);
	struct v4l2_mbus_framefmt mf;
	struct vin_fmt *ffmt = NULL;

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);
	ffmt = vin_find_format(&f->fmt.pix_mp.pixelformat, NULL,
		VIN_FMT_ALL, -1, false);
	if (ffmt == NULL) {
		vin_err("vin is not support this pixelformat\n");
		return -EINVAL;
	}

	mf.width = f->fmt.pix_mp.width;
	mf.height = f->fmt.pix_mp.height;
	mf.code = ffmt->mbus_code;
	vin_pipeline_try_format(vinc, &mf, &ffmt, true);

	f->fmt.pix_mp.width = mf.width;
	f->fmt.pix_mp.height = mf.height;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG;
	return 0;
}


static int __vin_set_fmt(struct vin_core *vinc, struct v4l2_format *f)
{
	struct vin_vid_cap *cap = &vinc->vid_cap;
	struct sensor_win_size win_cfg;
	struct v4l2_subdev_format sfmt;
	struct v4l2_mbus_framefmt mf;
	struct v4l2_crop win;
	struct vin_fmt *ffmt = NULL;
	struct mbus_framefmt_res *res = (void *)mf.reserved;

	int ret;

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);

	if (vin_streaming(cap)) {
		vin_err("%s device busy\n", __func__);
		return -EBUSY;
	}

	cap->frame.fmt = vin_find_format(&f->fmt.pix_mp.pixelformat, NULL,
					VIN_FMT_ALL, -1, false);
	ffmt = cap->frame.fmt;
	if (NULL == ffmt) {
		vin_err("vin does not support this pixelformat 0x%x\n",
				f->fmt.pix_mp.pixelformat);
		return -EINVAL;
	}
	mf.width = f->fmt.pix_mp.width;
	mf.height = f->fmt.pix_mp.height;
	mf.field = f->fmt.pix_mp.field;
	mf.code = ffmt->mbus_code;
	res->res_pix_fmt = f->fmt.pix_mp.pixelformat;
	ret = vin_pipeline_try_format(vinc, &mf, &ffmt, true);
	if (ret < 0) {
		vin_err("vin_pipeline_try_format failed\n");
		return -EINVAL;
	}

	cap->frame.fmt->field = mf.field;

	vin_print("vin_pipeline_try_format %d*%d %x %d\n",
		mf.width, mf.height, mf.code, mf.field);

	vin_pipeline_set_mbus_config(vinc);

	/*get current win configs*/
	memset(&win_cfg, 0, sizeof(struct sensor_win_size));
	ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR], core, ioctl,
			     GET_CURRENT_WIN_CFG, &win_cfg);
	if (ret == 0) {
		win.c.width = win_cfg.width;
		win.c.height = win_cfg.height;
		win.c.left = win_cfg.hoffset;
		win.c.top = win_cfg.voffset;
		ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_CSI], video,
					s_crop, &win);
		if (ret < 0) {
			vin_err("v4l2 sub device csi s_crop error!\n");
			goto out;
		}
	} else {
		vin_warn("get sensor win_cfg failed!\n");
	}

	if (vinc->mbus_type == V4L2_MBUS_CSI2) {
		res->res_mipi_bps = win_cfg.mipi_bps;
		sfmt.format.code = mf.code;
		sfmt.format.field = mf.field;
		sfmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		sfmt.pad = MIPI_PAD_SINK;
		ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_MIPI], pad,
					set_fmt, NULL, &sfmt);
		if (ret < 0) {
			vin_err("v4l2 sub device mipi set_fmt error!\n");
			goto out;
		}
	}

	if (cap->capture_mode == V4L2_MODE_IMAGE) {
		sunxi_flash_check_to_start(cap->pipe.sd[VIN_IND_FLASH],
					   SW_CTRL_FLASH_ON);
	} else {
		sunxi_flash_stop(vinc->vid_cap.pipe.sd[VIN_IND_FLASH]);
	}

	if ((mf.width < f->fmt.pix_mp.width) || (mf.height < f->fmt.pix_mp.height)) {
		f->fmt.pix_mp.width = mf.width;
		f->fmt.pix_mp.height = mf.height;
	}

	cap->frame.offs_h = (mf.width - f->fmt.pix_mp.width) / 2;
	cap->frame.offs_v = (mf.height - f->fmt.pix_mp.height) / 2;
	cap->frame.o_width = f->fmt.pix_mp.width;
	cap->frame.o_height = f->fmt.pix_mp.height;

	vinc->vin_status.width = cap->frame.o_width;
	vinc->vin_status.height = cap->frame.o_height;
	vinc->vin_status.pix_fmt = cap->frame.fmt->fourcc;

	ret = 0;
out:
	return ret;
}


static int vidioc_s_fmt_vid_cap_mplane(struct file *file, void *priv,
				       struct v4l2_format *f)
{
	struct vin_core *vinc = video_drvdata(file);
	return __vin_set_fmt(vinc, f);
}

int vidioc_g_crop(struct file *file, void *fh,
				struct v4l2_crop *a)
{
	struct vin_core *vinc = video_drvdata(file);

	a->c.left = vinc->vid_cap.frame.offs_h;
	a->c.top = vinc->vid_cap.frame.offs_v;
	a->c.width = vinc->vid_cap.frame.o_width;
	a->c.height = vinc->vid_cap.frame.o_height;

	return 0;
}

int vidioc_s_crop(struct file *file, void *fh,
				const struct v4l2_crop *a)
{
	struct vin_core *vinc = video_drvdata(file);

	vinc->vid_cap.frame.offs_h = a->c.left;
	vinc->vid_cap.frame.offs_v = a->c.top;
	vinc->vid_cap.frame.o_width = a->c.width;
	vinc->vid_cap.frame.o_height = a->c.height;

	return 0;
}

int vidioc_g_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct vin_core *vinc = video_drvdata(file);
	struct v4l2_subdev_selection sel;
	int ret = 0;

	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = SCALER_PAD_SINK;
	sel.target = s->target;
	sel.flags = s->flags;
	sel.r = s->r;
	ret = v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SCALER], pad,
				set_selection, NULL, &sel);
	if (ret < 0)
		vin_err("v4l2 sub device scaler set_selection error!\n");
	return ret;
}
int vidioc_s_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct vin_core *vinc = video_drvdata(file);
	struct v4l2_subdev_selection sel;
	int ret = 0;

	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = SCALER_PAD_SINK;
	sel.target = s->target;
	sel.flags = s->flags;
	ret = v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SCALER], pad,
				get_selection, NULL, &sel);
	if (ret < 0)
		vin_err("v4l2 sub device scaler get_selection error!\n");
	else
		s->r = sel.r;
	return ret;

}

static int vidioc_enum_fmt_vid_overlay(struct file *file, void *__fh,
					    struct v4l2_fmtdesc *f)
{
	struct vin_fmt *fmt;

	vin_log(VIN_LOG_VIDEO, "%s\n", __func__);
	fmt = vin_find_format(NULL, NULL, VIN_FMT_OSD, f->index, false);
	if (!fmt)
		return -EINVAL;

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int vidioc_g_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *f)
{
	struct vin_core *vinc = video_drvdata(file);

	f->fmt.win.w.left = 0;
	f->fmt.win.w.top = 0;
	f->fmt.win.w.width = vinc->vid_cap.frame.o_width;
	f->fmt.win.w.height = vinc->vid_cap.frame.o_height;
	f->fmt.win.clipcount = vinc->vid_cap.osd.clipcount;
	f->fmt.win.chromakey = vinc->vid_cap.osd.chromakey;

	return 0;
}
static void __osd_check_clipcount(struct v4l2_window *win)
{
	if (win->bitmap) {
		if (win->clipcount > MAX_OVERLAY)
			win->clipcount = MAX_OVERLAY;
	} else {
		if (win->clipcount > MAX_COVER)
			win->clipcount = MAX_COVER;
	}
	if (win->clipcount < 1)
		win->clipcount = 1;
}

static int vidioc_try_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *f)
{
	if (f->fmt.win.w.width > MAX_WIDTH)
		f->fmt.win.w.width = MAX_WIDTH;
	if (f->fmt.win.w.width < MIN_WIDTH)
		f->fmt.win.w.width = MIN_WIDTH;
	if (f->fmt.win.w.height > MAX_HEIGHT)
		f->fmt.win.w.height = MAX_HEIGHT;
	if (f->fmt.win.w.height < MIN_HEIGHT)
		f->fmt.win.w.height = MIN_HEIGHT;
	__osd_check_clipcount(&f->fmt.win);

	return 0;
}

void __osd_bitmap2dram(struct vin_osd *osd, void *databuf)
{
	int i, j, k, m = 0, n = 0;
	int kend = 0, idx = 0, ww = 0, ysn = 0;
	int y_num = 0, *y_temp = NULL;
	int *hor_num = NULL, *hor_index = NULL;
	int *x_temp = NULL, *xbuf = NULL, *x_idx = NULL;
	int addr_offset = 0, pix_size = 0;
	int cnt = osd->clipcount;
	void *dram_addr = osd->overlay_mask.vir_addr;

	y_temp = (int *)kzalloc(2 * cnt * sizeof(int), GFP_KERNEL);
	for (i = 0; i < cnt; i++) {
		y_temp[i] = osd->region[i].top;
		y_temp[i + cnt] = osd->region[i].top + osd->region[i].height - 1;
	}
	sort(y_temp, 2 * cnt, sizeof(int), vin_cmp, vin_swap);
	y_num = vin_unique(y_temp, 2 * cnt);
	hor_num = (int *)kzalloc(y_num * sizeof(int), GFP_KERNEL); /*0~y_num-1*/
	hor_index = (int *)kzalloc(y_num * cnt * sizeof(int), GFP_KERNEL); /*(0~y_num-1) * (0~N+1)*/

	for (j = 0; j < y_num; j++) {
		ysn = 0;
		for (i = 0; i < cnt; i++) {
			if (osd->region[i].top <= y_temp[j] &&
			   (osd->region[i].top + osd->region[i].height) > y_temp[j]) {
				hor_num[j]++;
				hor_index[j * cnt + ysn] = i;
				ysn = ysn + 1;
			}
		}
	}

	for (j = 0; j < y_num; j++) {
		x_temp = (int *)kzalloc(hor_num[j] * sizeof(int), GFP_KERNEL);
		xbuf = (int *)kzalloc(hor_num[j] * sizeof(int), GFP_KERNEL);
		x_idx = (int *)kzalloc(hor_num[j] * sizeof(int), GFP_KERNEL);
		for (k = 0; k < hor_num[j]; k++) {
			x_temp[k] = osd->region[hor_index[j * cnt + k]].left;
		}
		memcpy(xbuf, x_temp, hor_num[j] * sizeof(int));
		sort(x_temp, hor_num[j], sizeof(int), vin_cmp, vin_swap);

		for (k = 0; k < hor_num[j]; k++)	{
			for (m = 0; m < hor_num[j]; m++) {
				if (x_temp[k] == xbuf[m]) {
					x_idx[k] = m;
					break;
				}
			}
		}

		if (j == y_num - 1)
			kend = y_temp[j];
		else
			kend = y_temp[j + 1] - 1;
		for (k = y_temp[j]; k <= kend; k++) {
			for (i = 0; i < hor_num[j]; i++)	{
				idx = hor_index[j * cnt + x_idx[i]];
				addr_offset = 0;
				for (n = 0; n < idx; n++)
					addr_offset +=	(osd->region[n].width * osd->region[n].height) * pix_size;
				ww = osd->region[idx].width;
				if (k < (osd->region[idx].top + osd->region[idx].height)) {
					pix_size = osd->fmt->depth[0]/8;
					memcpy(dram_addr, databuf + addr_offset
						+ ww * (k - osd->region[idx].top) * pix_size,
						ww * pix_size);
					dram_addr += ww * pix_size;
				}
			}
		}
		kfree(x_temp);
		kfree(xbuf);
		kfree(x_idx);
		x_temp = NULL;
		xbuf = NULL;
		x_idx = NULL;
	}
	kfree(hor_num);
	kfree(hor_index);
	kfree(y_temp);
	y_temp = NULL;
	hor_index = NULL;
	hor_num = NULL;
}

static int vidioc_s_fmt_vid_overlay(struct file *file, void *__fh,
					struct v4l2_format *f)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_osd *osd = &vinc->vid_cap.osd;
	struct v4l2_clip *clip = NULL;
	void *bitmap = NULL;
	unsigned int bitmap_size = 0, pix_size = 0;
	int ret = 0, i = 0;

	vin_log(VIN_LOG_VIDEO, "cnt=%d, key=%d, map=0x%p\n",
			f->fmt.win.clipcount,
			f->fmt.win.chromakey,
			f->fmt.win.bitmap);

	__osd_check_clipcount(&f->fmt.win);

	osd->clipcount = f->fmt.win.clipcount;

	clip = vmalloc(sizeof(struct v4l2_clip) * (osd->clipcount + 4));
	if (clip == NULL) {
		vin_err("%s - Alloc of clip mask failed\n", __func__);
		return -ENOMEM;
	}
	if (copy_from_user(clip, f->fmt.win.clips,
		sizeof(struct v4l2_clip) * osd->clipcount)) {
		vfree(clip);
		return -EFAULT;
	}

	for (i = 0; i < osd->clipcount; i++) {
		osd->region[i] = clip[i].c;
		bitmap_size += clip[i].c.width * clip[i].c.height;
		vin_log(VIN_LOG_VIDEO, "region: %d. %d. %d. %d\n",
			clip[i].c.left, clip[i].c.top,
			clip[i].c.width, clip[i].c.height);
	}
	vfree(clip);

	osd->chromakey = f->fmt.win.chromakey;

	if (f->fmt.win.bitmap) {
		osd->is_ov_cv = 0;
		osd->fmt = vin_find_format(&f->fmt.win.chromakey, NULL,
				VIN_FMT_OSD, -1, false);
		if (osd->fmt == NULL) {
			vin_err("osd is not support this chromakey\n");
			return -EINVAL;
		}
		pix_size = osd->fmt->depth[0]/8;

		bitmap = vmalloc(bitmap_size * pix_size);
		if (bitmap == NULL) {
			vin_err("%s - Alloc of bitmap buf failed\n", __func__);
			return -ENOMEM;
		}
		if (copy_from_user(bitmap, f->fmt.win.bitmap,
				bitmap_size * pix_size)) {
			vfree(bitmap);
			return -EFAULT;
		}
		if (f->fmt.win.global_alpha > 17)
			f->fmt.win.global_alpha = 17;
		osd->global_alpha[0] = f->fmt.win.global_alpha;

		vin_log(VIN_LOG_VIDEO, "bitmap_size %d, pix_size %d, alpha %d\n",
			bitmap_size, pix_size, f->fmt.win.global_alpha);
	} else {
		osd->is_ov_cv = 1;
		for (i = 0; i < osd->clipcount; i++) {
			switch (osd->chromakey) {
			case 0:
				osd->yuv_cover[0][i] = 64;
				osd->yuv_cover[1][i] = 0;
				osd->yuv_cover[2][i] = 0;
				break;
			case 1:
				osd->yuv_cover[0][i] = 128;
				osd->yuv_cover[1][i] = 0;
				osd->yuv_cover[2][i] = 0;
				break;
			case 2:
				osd->yuv_cover[0][i] = 192;
				osd->yuv_cover[1][i] = 0;
				osd->yuv_cover[2][i] = 0;
				break;
			default:
				osd->yuv_cover[0][i] = 64;
				osd->yuv_cover[1][i] = 0;
				osd->yuv_cover[2][i] = 0;
				break;
			}
		}
	}
	if (osd->overlay_mask.size != bitmap_size * pix_size) {
		if (osd->overlay_mask.phy_addr) {
			os_mem_free(&osd->overlay_mask);
			osd->overlay_mask.phy_addr = NULL;
		}
		osd->overlay_mask.size = bitmap_size * pix_size;
		os_mem_alloc(&osd->overlay_mask);
		memset(osd->overlay_mask.vir_addr, 0, bitmap_size * pix_size);
		if (osd->overlay_mask.phy_addr != NULL) {
			if (!osd->overlay_mask.vir_addr) {
				vin_err("osd bitmap load vaddr requset failed!\n");
				return -ENOMEM;
			}
		} else {
			vin_err("osd bitmap load paddr requset failed!\n");
			return -ENOMEM;
		}
	}

	__osd_bitmap2dram(osd, bitmap);
	vfree(bitmap);
	osd->is_set = 0;

	return ret;
}

static int __osd_reg_setup(int id, struct vin_osd *osd, unsigned int on)
{
	struct vipp_osd_config osd_cfg;
	struct vipp_osd_para_config para;
	struct vipp_rgb2yuv_factor rgb2yuv_def = {
		.jc0 = 0x107,
		.jc1 = 0x204,
		.jc2 = 0x64,
		.jc3 = 0x768,
		.jc4 = 0x6d6,
		.jc5 = 0x1c2,
		.jc6 = 0x687,
		.jc7 = 0x1c2,
		.jc8 = 0x7b7,
		.jc9 = 0x10,
		.jc10 = 0x80,
		.jc11 = 0x80,
	};
	int i;

	memset(&osd_cfg, 0, sizeof(osd_cfg));
	memset(&para, 0, sizeof(para));
	if (osd->is_ov_cv == 0) {
		switch (osd->chromakey) {
		case V4L2_PIX_FMT_RGB555:
			osd_cfg.osd_argb_mode = ARGB1555;
			break;
		case V4L2_PIX_FMT_RGB444:
			osd_cfg.osd_argb_mode = ARGB4444;
			break;
			break;
		case V4L2_PIX_FMT_RGB32:
			osd_cfg.osd_argb_mode = ARGB8888;
			break;
			break;
		default:
			osd_cfg.osd_argb_mode = ARGB8888;
			break;
		}
		osd_cfg.osd_ov_num = osd->clipcount;
		osd_cfg.osd_ov_en = 1;
		osd_cfg.osd_stat_en = 1;
		for (i = 0; i < osd_cfg.osd_ov_num; i++) {
			para.overlay_cfg[i].h_start = osd->region[i].left;
			para.overlay_cfg[i].h_end = osd->region[i].left + osd->region[i].width - 1;
			para.overlay_cfg[i].v_start = osd->region[i].top;
			para.overlay_cfg[i].v_end = osd->region[i].top + osd->region[i].height - 1;
			para.overlay_cfg[i].alpha = osd->global_alpha[0];
			para.overlay_cfg[i].inverse_en = 0;
		}
	} else {
		osd_cfg.osd_cv_num = osd->clipcount;
		osd_cfg.osd_cv_en = 1;
		for (i = 0; i < osd_cfg.osd_cv_num; i++) {
			para.cover_cfg[i].h_start = osd->region[i].left;
			para.cover_cfg[i].h_end = osd->region[i].left + osd->region[i].width - 1;
			para.cover_cfg[i].v_start = osd->region[i].top;
			para.cover_cfg[i].v_end = osd->region[i].top + osd->region[i].height - 1;
			para.cover_data[i].y = osd->yuv_cover[0][i];
			para.cover_data[i].u = osd->yuv_cover[1][i];
			para.cover_data[i].v = osd->yuv_cover[2][i];
		}
	}

	vipp_osd_cfg(id, &osd_cfg);
	vipp_osd_rgb2yuv(id, &rgb2yuv_def);
	vipp_osd_para_cfg(id, &para, &osd_cfg);
	return 0;
}

static int vidioc_overlay(struct file *file, void *__fh, unsigned int on)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_osd *osd = &vinc->vid_cap.osd;
	int ret = 0;

	if (!on) {
		if (osd->overlay_mask.phy_addr) {
			os_mem_free(&osd->overlay_mask);
			osd->overlay_mask.phy_addr = NULL;
		}
		vipp_chroma_ds_en(vinc->vipp_sel, 0);
		vipp_osd_en(vinc->vipp_sel, 0);
		goto para_ready;
	} else {
		/*when osd enable scaler down outfmt must be YUV422*/
		vipp_scaler_output_fmt(vinc->vipp_sel, YUV422);
		vipp_osd_en(vinc->vipp_sel, 1);
		switch (vinc->vid_cap.frame.fmt->fourcc) {
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YUV420M:
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_YVU420M:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV21M:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV12M:
			vipp_chroma_ds_en(vinc->vipp_sel, 1);
			break;
		default:
			break;
		}
	}
	if (osd->is_set)
		return ret;
	ret = __osd_reg_setup(vinc->vipp_sel, osd, on);
	vipp_set_osd_bm_load_addr(vinc->vipp_sel, (unsigned long)osd->overlay_mask.dma_addr);

	if (osd->is_ov_cv == 0)
		vipp_set_osd_ov_update(vinc->vipp_sel, HAS_UPDATED);
	else
		vipp_set_osd_cv_update(vinc->vipp_sel, HAS_UPDATED);
para_ready:
	vipp_set_para_ready(vinc->vipp_sel, HAS_READY);
	osd->is_set = 1;
	return ret;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	struct vin_buffer *buf;
	int ret = 0;

	vin_print("%s\n", __func__);
	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = -EINVAL;
		goto streamon_error;
	}

	if (vin_streaming(cap)) {
		vin_err("stream has been already on\n");
		ret = -1;
		goto streamon_error;
	}
#if USE_BIG_PIPELINE
	/*
	 * when media_entity_pipeline_start is called, the diffrent video
	 * output which have same input maybe in the same pipeline. this
	 * situation is not good for our design. furthermore in our design
	 * the same subdev would in diffrent pipeline, which is not allow
	 * in media_entity_pipeline_start, so we give up it.
	 */
	 /*the big pipeline would use it*/
	ret = media_entity_pipeline_start(&cap->vdev.entity, &vinc->vid_cap.pipe.pipe);
	if (ret < 0)
		goto streamon_error;
#endif
	ret = vb2_ioctl_streamon(file, priv, i);
	if (ret)
		goto streamon_error;

	buf = list_entry(cap->vidq_active.next, struct vin_buffer, list);

	vin_set_addr(vinc, &buf->vb, &vinc->vid_cap.frame,
			&vinc->vid_cap.frame.paddr);
	vin_print("call set_stream task, %s!\n", __func__);
	schedule_work(&vinc->vid_cap.s_stream_task);

streamon_error:

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	int ret = 0;

	vin_print("%s\n", __func__);
	if (!vin_streaming(cap)) {
		vin_err("stream has been already off\n");
		ret = 0;
		goto streamoff_error;
	}

	vin_pipeline_call(vinc, set_stream, &cap->pipe, 0);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = -EINVAL;
		goto streamoff_error;
	}

	ret = vb2_ioctl_streamoff(file, priv, i);
	if (ret != 0) {
		vin_err("%s error!\n", __func__);
		goto streamoff_error;
	}
#if USE_BIG_PIPELINE
	media_entity_pipeline_stop(&cap->vdev.entity);
#endif
streamoff_error:

	return ret;
}

static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_UNKNOWN;
	strcpy(inp->name, "sunxi-vin");

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	vin_log(VIN_LOG_VIDEO, "input_num = %d\n", i);
	return i == 0 ? i : -EINVAL;
}

static const char *const sensor_info_type[] = {
	"YUV",
	"RAW",
	NULL,
};

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parms)
{
	struct vin_core *vinc = video_drvdata(file);
	int ret;

	ret =
	    v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SENSOR], video,
			     g_parm, parms);
	if (ret < 0)
		vin_warn("v4l2 sub device g_parm fail!\n");

	return ret;
}

static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parms)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	int ret = 0, valid_idx = vinc->modu_cfg.sensors.valid_idx;

	struct sensor_instance *inst = &vinc->modu_cfg.sensors.inst[valid_idx];

	if (parms->parm.capture.capturemode != V4L2_MODE_VIDEO &&
	    parms->parm.capture.capturemode != V4L2_MODE_IMAGE &&
	    parms->parm.capture.capturemode != V4L2_MODE_PREVIEW) {
		parms->parm.capture.capturemode = V4L2_MODE_PREVIEW;
	}

	cap->capture_mode = parms->parm.capture.capturemode;

	ret =
	    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR], video, s_parm,
				parms);
	if (ret < 0)
		vin_warn("v4l2 subdev sensor s_parm error!\n");

	ret =
	    v4l2_subdev_call(cap->pipe.sd[VIN_IND_CSI], video, s_parm,
				parms);
	if (ret < 0)
		vin_warn("v4l2 subdev csi s_parm error!\n");

	if (inst->is_isp_used) {
		ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_ISP], video, s_parm,
					parms);
		if (ret < 0)
			vin_warn("v4l2 subdev isp s_parm error!\n");
	}

	return ret;
}

int isp_ae_stat_req(struct file *file, struct v4l2_fh *fh,
		    struct isp_stat_buf *ae_buf)
{
	struct vin_vid_cap *cap =
	    container_of(video_devdata(file), struct vin_vid_cap, vdev);
	struct isp_dev *isp = v4l2_get_subdevdata(cap->pipe.sd[VIN_IND_ISP]);

	int ret = 0;
	ae_buf->buf_size = ISP_STAT_AE_MEM_SIZE;
	ret = copy_to_user(ae_buf->buf, isp->stat_buf->ae_buf, ae_buf->buf_size);
	return ret;
}

int isp_gamma_req(struct file *file, struct v4l2_fh *fh,
		  struct isp_stat_buf *gamma_buf)
{
	int ret = 0;
	gamma_buf->buf_size = ISP_GAMMA_MEM_SIZE;
	return ret;
}

int isp_hist_stat_req(struct file *file, struct v4l2_fh *fh,
		      struct isp_stat_buf *hist_buf)
{
	struct vin_vid_cap *cap =
	    container_of(video_devdata(file), struct vin_vid_cap, vdev);
	struct isp_dev *isp = v4l2_get_subdevdata(cap->pipe.sd[VIN_IND_ISP]);

	int ret = 0;
	hist_buf->buf_size = ISP_STAT_HIST_MEM_SIZE;
	ret = copy_to_user(hist_buf->buf, isp->stat_buf->hist_buf,
			   hist_buf->buf_size);
	return ret;
}

int isp_af_stat_req(struct file *file, struct v4l2_fh *fh,
		    struct isp_stat_buf *af_buf)
{
	struct vin_vid_cap *cap =
	    container_of(video_devdata(file), struct vin_vid_cap, vdev);
	struct isp_dev *isp = v4l2_get_subdevdata(cap->pipe.sd[VIN_IND_ISP]);

	int ret = 0;
	af_buf->buf_size = ISP_STAT_AF_MEM_SIZE;

	ret = copy_to_user(af_buf->buf, isp->stat_buf->af_buf, af_buf->buf_size);
	return 0;
}

static int isp_exif_req(struct file *file, struct v4l2_fh *fh,
			struct isp_exif_attribute *exif_attr)
{
	struct vin_core *vinc = video_drvdata(file);
	struct sensor_instance *inst = get_valid_sensor(vinc);
	struct v4l2_subdev *sd = vinc->vid_cap.pipe.sd[VIN_IND_SENSOR];
	struct sensor_exif_attribute exif;

	if (inst->is_bayer_raw)
		return 0;
	if (v4l2_subdev_call(sd, core, ioctl, GET_SENSOR_EXIF, &exif)) {
		exif_attr->fnumber = 240;
		exif_attr->focal_length = 180;
		exif_attr->brightness = 128;
		exif_attr->flash_fire = 0;
		exif_attr->iso_speed = 200;
		exif_attr->exposure_time.numerator = 1;
		exif_attr->exposure_time.denominator = 20;
		exif_attr->shutter_speed.numerator = 1;
		exif_attr->shutter_speed.denominator = 24;
	} else {
		exif_attr->fnumber = exif.fnumber;
		exif_attr->focal_length = exif.focal_length;
		exif_attr->brightness = exif.brightness;
		exif_attr->flash_fire = exif.flash_fire;
		exif_attr->iso_speed = exif.iso_speed;
		exif_attr->exposure_time.numerator = exif.exposure_time_num;
		exif_attr->exposure_time.denominator = exif.exposure_time_den;
		exif_attr->shutter_speed.numerator = exif.exposure_time_num;
		exif_attr->shutter_speed.denominator = exif.exposure_time_den;
	}
	return 0;
}

static int __vin_sensor_set_af_win(struct vin_vid_cap *cap)
{
	struct vin_pipeline *pipe = &cap->pipe;
	struct v4l2_win_setting af_win;
	int ret = 0;
	af_win.coor.x1 = cap->af_win[0]->val;
	af_win.coor.y1 = cap->af_win[1]->val;
	af_win.coor.x2 = cap->af_win[2]->val;
	af_win.coor.y2 = cap->af_win[3]->val;

	ret = v4l2_subdev_call(pipe->sd[VIN_IND_SENSOR],
				core, ioctl, SET_AUTO_FOCUS_WIN, &af_win);
	return ret;
}

static int __vin_sensor_set_ae_win(struct vin_vid_cap *cap)
{
	struct vin_pipeline *pipe = &cap->pipe;
	struct v4l2_win_setting ae_win;
	int ret = 0;
	ae_win.coor.x1 = cap->ae_win[0]->val;
	ae_win.coor.y1 = cap->ae_win[1]->val;
	ae_win.coor.x2 = cap->ae_win[2]->val;
	ae_win.coor.y2 = cap->ae_win[3]->val;
	ret = v4l2_subdev_call(pipe->sd[VIN_IND_SENSOR],
				core, ioctl, SET_AUTO_EXPOSURE_WIN, &ae_win);
	return ret;
}
int vidioc_hdr_ctrl(struct file *file, struct v4l2_fh *fh,
		    struct isp_hdr_ctrl *hdr)
{
	struct vin_core *vinc = video_drvdata(file);
	struct v4l2_event ev;
	struct vin_isp_hdr_event_data *ed = (void *)ev.u.data;

	memset(&ev, 0, sizeof(ev));
	ev.type = V4L2_EVENT_VIN_CLASS;
	if (vinc->modu_cfg.sensors.inst[0].is_bayer_raw) {
		if (hdr->flag == HDR_CTRL_SET) {
			ed->cmd = HDR_CTRL_SET;
			ed->hdr = *hdr;
		} else {
			ed->cmd = HDR_CTRL_GET;
		}
		v4l2_event_queue(video_devdata(file), &ev);
		return 0;
	}
	return -EINVAL;
}

static long vin_param_handler(struct file *file, void *priv,
			      bool valid_prio, unsigned int cmd, void *param)
{
	int ret = 0;
	struct v4l2_fh *fh = (struct v4l2_fh *)priv;
	struct isp_stat_buf *stat = (struct isp_stat_buf *)param;

	switch (cmd) {
	case VIDIOC_ISP_AE_STAT_REQ:
		ret = isp_ae_stat_req(file, fh, stat);
		break;
	case VIDIOC_ISP_AF_STAT_REQ:
		ret = isp_af_stat_req(file, fh, stat);
		break;
	case VIDIOC_ISP_HIST_STAT_REQ:
		ret = isp_hist_stat_req(file, fh, stat);
		break;
	case VIDIOC_ISP_EXIF_REQ:
		ret =
		    isp_exif_req(file, fh, (struct isp_exif_attribute *)param);
		break;
	case VIDIOC_ISP_GAMMA_REQ:
		ret = isp_gamma_req(file, fh, stat);
		break;
	case VIDIOC_HDR_CTRL:
		ret = vidioc_hdr_ctrl(file, fh, (struct isp_hdr_ctrl *)param);
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}

static int vin_subscribe_event(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	vin_print("%s id = %d\n", __func__, sub->id);

	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_ctrl_subscribe_event(fh, sub);
	else
		return v4l2_event_subscribe(fh, sub, 1, NULL);
}

static int vin_unsubscribe_event(struct v4l2_fh *fh,
	const struct v4l2_event_subscription *sub)
{
	vin_print("%s id = %d\n", __func__, sub->id);
	return v4l2_event_unsubscribe(fh, sub);
}

static int vin_video_set_default_format(struct vin_core *vinc)
{

	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.fmt.pix_mp = {
			.width		= 800,
			.height		= 600,
			.pixelformat	= V4L2_PIX_FMT_YUV420M,
			.field		= V4L2_FIELD_NONE,
			.colorspace	= V4L2_COLORSPACE_JPEG,
		},
	};
	return __vin_set_fmt(vinc, &fmt);
}
static int __vin_sensor_setup_link(struct vin_core *vinc, int i, int en)
{
	struct v4l2_subdev *sensor = NULL;
	struct media_link *link = NULL;
	int ret;
	sensor = vinc->modu_cfg.modules.sensor[i].sd;
	if (sensor == NULL)
		return -1;

	link = &sensor->entity.links[0];
	if (link == NULL)
		return -1;

	vin_print("setup link: [%s] %c> [%s]\n",
		sensor->name, en ? '=' : '-',
		link->sink->entity->name);
	if (en)
		ret = media_entity_setup_link(link, MEDIA_LNK_FL_ENABLED);
	else
		ret = media_entity_setup_link(link, 0);

	if (ret) {
		vin_warn("%s setup link %s fail!\n", sensor->name,
				link->sink->entity->name);
		return -1;
	}
	return 0;
}

static int vin_open(struct file *file)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	int valid_idx = vinc->modu_cfg.sensors.valid_idx;
	struct sensor_instance *inst = NULL;
	struct sensor_item sensor_info;
	unsigned long core_clk;
	int ret;

	vin_print("%s\n", __func__);
	if (vin_busy(cap)) {
		vin_err("device open busy\n");
		ret = -EBUSY;
		goto open_end;
	}
#ifdef CONFIG_DEVFREQ_DRAM_FREQ_WITH_SOFT_NOTIFY
	dramfreq_master_access(MASTER_CSI, true);
#endif
	if (-1 == valid_idx) {
		vin_err("there is no valid sensor\n");
		ret = -EINVAL;
		goto open_end;
	}

	if (__vin_sensor_setup_link(vinc, valid_idx, 1) < 0) {
		vin_err("sensor setup link failed\n");
		ret = -EINVAL;
		goto open_end;
	}
	inst = &vinc->modu_cfg.sensors.inst[valid_idx];

	sunxi_isp_sensor_type(cap->pipe.sd[VIN_IND_ISP], inst->is_isp_used, inst->is_bayer_raw);
	vinc->support_raw = inst->is_isp_used;

	csi_cci_init_helper(vinc->modu_cfg.bus_sel);

	cap->first_flag = 0;
	set_bit(VIN_BUSY, &cap->state);
	/* create event queue */
	v4l2_fh_open(file);
	vinc->vin_status.frame_cnt = 0;

	/*set vin core clk rate for each sensor!*/
	if (get_sensor_info(inst->cam_name, &sensor_info) == 0)
		core_clk = sensor_info.core_clk_for_sensor;
	else
		core_clk = CSI_CORE_CLK_RATE;

	ret = vin_pipeline_call(vinc, open, &cap->pipe, &cap->vdev.entity, true);
	if (ret < 0) {
		vin_err("vin pipeline open failed (%d)!\n", ret);
		goto open_end;
	}

	v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_CSI], core, ioctl,
			 VIDIOC_SUNXI_CSI_SET_CORE_CLK, &core_clk);

	/*must be after ahb and core clock enable*/
	ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_ISP], core, init, 1);
	if (ret < 0) {
		vin_err("ISP init error at %s\n", __func__);
		return ret;
	}

	ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_SCALER], core, init, 1);
	if (ret < 0) {
		vin_err("SCALER init error at %s\n", __func__);
		return ret;
	}

	ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR], core, init, 1);
	if (ret) {
		vin_err("sensor initial error when selecting target device!\n");
		goto open_end;
	}

	vinc->hflip = inst->hflip;
	vinc->vflip = inst->vflip;

	vin_video_set_default_format(vinc);

open_end:
	if (ret)
		vin_print("%s fail!\n", __func__);
	else
		vin_print("%s ok!\n", __func__);
	return ret;
}

static int vin_close(struct file *file)
{
	struct vin_core *vinc = video_drvdata(file);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	int valid_idx = vinc->modu_cfg.sensors.valid_idx;
	int ret;
	vin_print("%s\n", __func__);
	if (!vin_busy(cap)) {
		vin_warn("device have been closed!\n");
		return 0;
	}
	clear_bit(VIN_STREAM, &cap->state);
	vin_pipeline_call(vinc, set_stream, &cap->pipe, 0);
	if (vinc->vid_cap.pipe.sd[VIN_IND_ACTUATOR] != NULL)
		v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_ACTUATOR], core,
				 ioctl, ACT_SOFT_PWDN, 0);
	ret = vin_pipeline_call(vinc, close, &cap->pipe);
	if (ret)
		vin_err("vin pipeline close failed!\n");

	/*must be after ahb and core clock enable*/
	ret = v4l2_subdev_call(cap->pipe.sd[VIN_IND_ISP], core, init, 0);
	if (ret < 0) {
		vin_err("ISP deinit error at %s\n", __func__);
		return ret;
	}
	/*software*/
	vb2_fop_release(file); /*vb2_queue_release(&cap->vb_vidq);*/
	clear_bit(VIN_BUSY, &cap->state);
	__vin_sensor_setup_link(vinc, valid_idx, 0);
#ifdef CONFIG_DEVFREQ_DRAM_FREQ_WITH_SOFT_NOTIFY
	dramfreq_master_access(MASTER_CSI, false);
#endif
	vin_print("vin_close end\n");
	return 0;
}

static int vin_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct vin_vid_cap *cap =
	    container_of(ctrl->handler, struct vin_vid_cap, ctrl_handler);
	struct vin_core *vinc = cap->vinc;
	struct sensor_instance *inst = get_valid_sensor(vinc);
	struct v4l2_control c;
	c.id = ctrl->id;

	if (inst->is_isp_used && inst->is_bayer_raw) {
		switch (ctrl->id) {
		case V4L2_CID_EXPOSURE:
		case V4L2_CID_GAIN:
		case V4L2_CID_HOR_VISUAL_ANGLE:
		case V4L2_CID_VER_VISUAL_ANGLE:
		case V4L2_CID_FOCUS_LENGTH:
		case V4L2_CID_3A_LOCK:
		case V4L2_CID_AUTO_FOCUS_STATUS: /*Read-Only*/
		case V4L2_CID_SENSOR_TYPE:
			break;
		default:
			return -EINVAL;
		}
		return ret;
	} else {
		switch (ctrl->id) {
		case V4L2_CID_SENSOR_TYPE:
			c.value = inst->is_bayer_raw;
			break;
		case V4L2_CID_FLASH_LED_MODE:
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_FLASH],
						core, g_ctrl, &c);
			break;
		case V4L2_CID_AUTO_FOCUS_STATUS:
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, g_ctrl, &c);
			if (c.value != V4L2_AUTO_FOCUS_STATUS_BUSY)
				sunxi_flash_stop(cap->pipe.sd[VIN_IND_FLASH]);
			break;
		default:
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, g_ctrl, &c);
			break;
		}
		ctrl->val = c.value;
		if (ret < 0)
			vin_warn("v4l2 sub device g_ctrl fail!\n");
	}
	return ret;
}

static int vin_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vin_vid_cap *cap =
	    container_of(ctrl->handler, struct vin_vid_cap, ctrl_handler);
	struct vin_core *vinc = cap->vinc;
	struct sensor_instance *inst = get_valid_sensor(vinc);
	int ret = 0;
	struct actuator_ctrl_word_t vcm_ctrl;
	struct v4l2_control c;
	c.id = ctrl->id;
	c.value = ctrl->val;

	vin_log(VIN_LOG_VIDEO, "s_ctrl: %s, value: %d\n",
		ctrl->name, ctrl->val);

	if (inst->is_isp_used && inst->is_bayer_raw) {
		switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
		case V4L2_CID_CONTRAST:
		case V4L2_CID_SATURATION:
		case V4L2_CID_HUE:
		case V4L2_CID_AUTO_WHITE_BALANCE:
		case V4L2_CID_EXPOSURE:
		case V4L2_CID_AUTOGAIN:
		case V4L2_CID_GAIN:
		case V4L2_CID_POWER_LINE_FREQUENCY:
		case V4L2_CID_HUE_AUTO:
		case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
		case V4L2_CID_SHARPNESS:
		case V4L2_CID_CHROMA_AGC:
		case V4L2_CID_COLORFX:
		case V4L2_CID_AUTOBRIGHTNESS:
		case V4L2_CID_BAND_STOP_FILTER:
		case V4L2_CID_ILLUMINATORS_1:
		case V4L2_CID_ILLUMINATORS_2:
		case V4L2_CID_EXPOSURE_AUTO:
		case V4L2_CID_EXPOSURE_ABSOLUTE:
		case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
		case V4L2_CID_FOCUS_ABSOLUTE:
		case V4L2_CID_FOCUS_RELATIVE:
		case V4L2_CID_FOCUS_AUTO:
		case V4L2_CID_AUTO_EXPOSURE_BIAS:
		case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		case V4L2_CID_WIDE_DYNAMIC_RANGE:
		case V4L2_CID_IMAGE_STABILIZATION:
		case V4L2_CID_ISO_SENSITIVITY:
		case V4L2_CID_ISO_SENSITIVITY_AUTO:
		case V4L2_CID_EXPOSURE_METERING:
		case V4L2_CID_SCENE_MODE:
		case V4L2_CID_3A_LOCK:
		case V4L2_CID_AUTO_FOCUS_START:
		case V4L2_CID_AUTO_FOCUS_STOP:
		case V4L2_CID_AUTO_FOCUS_RANGE:
		case V4L2_CID_FLASH_LED_MODE:
		case V4L2_CID_AUTO_FOCUS_INIT:
		case V4L2_CID_AUTO_FOCUS_RELEASE:
		case V4L2_CID_GSENSOR_ROTATION:
		case V4L2_CID_TAKE_PICTURE:
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (ret < 0)
			vin_warn("v4l2 isp s_ctrl fail!\n");
	} else {
		switch (ctrl->id) {
		case V4L2_CID_FOCUS_ABSOLUTE:
			vcm_ctrl.code = ctrl->val;
			vcm_ctrl.sr = 0x0;
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_ACTUATOR],
						core, ioctl,
						ACT_SET_CODE, &vcm_ctrl);
			break;
		case V4L2_CID_FLASH_LED_MODE:
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_FLASH],
						core, s_ctrl, &c);
			break;
		case V4L2_CID_AUTO_FOCUS_START:
			sunxi_flash_check_to_start(cap->pipe.sd[VIN_IND_FLASH],
						SW_CTRL_TORCH_ON);
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, s_ctrl, &c);
			break;
		case V4L2_CID_AUTO_FOCUS_STOP:
			sunxi_flash_stop(cap->pipe.sd[VIN_IND_FLASH]);
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, s_ctrl, &c);
			break;
		case V4L2_CID_AE_WIN_X1:
			ret = __vin_sensor_set_ae_win(cap);
			break;
		case V4L2_CID_AF_WIN_X1:
			ret = __vin_sensor_set_af_win(cap);
			break;
		case V4L2_CID_AUTO_EXPOSURE_BIAS:
			c.value = ctrl->qmenu_int[ctrl->val];
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, s_ctrl, &c);
			break;
		default:
			ret =
			    v4l2_subdev_call(cap->pipe.sd[VIN_IND_SENSOR],
						core, s_ctrl, &c);
			break;
		}
		if (ret < 0)
			vin_warn("v4l2 sensor s_ctrl fail!\n");
	}
	return ret;
}

#ifdef CONFIG_COMPAT
struct isp_stat_buf32 {
	compat_caddr_t buf;
	__u32 buf_size;
};
static int get_isp_stat_buf32(struct isp_stat_buf *kp,
			      struct isp_stat_buf32 __user *up)
{
	u32 tmp;
	if (!access_ok(VERIFY_READ, up, sizeof(struct isp_stat_buf32)) ||
	    get_user(kp->buf_size, &up->buf_size) || get_user(tmp, &up->buf))
		return -EFAULT;
	kp->buf = compat_ptr(tmp);
	return 0;
}
static int put_isp_stat_buf32(struct isp_stat_buf *kp,
			      struct isp_stat_buf32 __user *up)
{
	u32 tmp = (u32) ((unsigned long)kp->buf);
	if (!access_ok(VERIFY_WRITE, up, sizeof(struct isp_stat_buf32)) ||
	    put_user(kp->buf_size, &up->buf_size) || put_user(tmp, &up->buf))
		return -EFAULT;
	return 0;
}

#define VIDIOC_ISP_AE_STAT_REQ32 _IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct isp_stat_buf32)
#define VIDIOC_ISP_HIST_STAT_REQ32 _IOWR('V', BASE_VIDIOC_PRIVATE + 2, struct isp_stat_buf32)
#define VIDIOC_ISP_AF_STAT_REQ32 _IOWR('V', BASE_VIDIOC_PRIVATE + 3, struct isp_stat_buf32)
#define VIDIOC_ISP_GAMMA_REQ32 _IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct isp_stat_buf32)

static long vin_compat_ioctl32(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	union {
		struct isp_stat_buf isb;
	} karg;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;
	switch (cmd) {
	case VIDIOC_ISP_AE_STAT_REQ32:
		cmd = VIDIOC_ISP_AE_STAT_REQ;
		break;
	case VIDIOC_ISP_HIST_STAT_REQ32:
		cmd = VIDIOC_ISP_HIST_STAT_REQ;
		break;
	case VIDIOC_ISP_AF_STAT_REQ32:
		cmd = VIDIOC_ISP_AF_STAT_REQ;
		break;
	case VIDIOC_ISP_GAMMA_REQ32:
		cmd = VIDIOC_ISP_GAMMA_REQ;
		break;
	}
	switch (cmd) {
	case VIDIOC_ISP_AE_STAT_REQ:
	case VIDIOC_ISP_HIST_STAT_REQ:
	case VIDIOC_ISP_AF_STAT_REQ:
	case VIDIOC_ISP_GAMMA_REQ:
		err = get_isp_stat_buf32(&karg.isb, up);
		compatible_arg = 0;
		break;
	}
	if (err)
		return err;
	if (compatible_arg)
		err = video_ioctl2(file, cmd, (unsigned long)up);
	else {
		mm_segment_t old_fs = get_fs();
		set_fs(KERNEL_DS);
		if (file->f_op->unlocked_ioctl)
			err =
			    file->f_op->unlocked_ioctl(file, cmd,
						       (unsigned long)&karg);
		else
			err = -ENOIOCTLCMD;
		set_fs(old_fs);
	}
	switch (cmd) {
	case VIDIOC_ISP_AE_STAT_REQ:
	case VIDIOC_ISP_HIST_STAT_REQ:
	case VIDIOC_ISP_AF_STAT_REQ:
	case VIDIOC_ISP_GAMMA_REQ:
		if (put_isp_stat_buf32(&karg.isb, up))
			err = -EFAULT;
		break;
	}
	return err;
}
#endif
/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

static const struct v4l2_ctrl_ops vin_ctrl_ops = {
	.g_volatile_ctrl = vin_g_volatile_ctrl,
	.s_ctrl = vin_s_ctrl,
};

static const struct v4l2_file_operations vin_fops = {
	.owner = THIS_MODULE,
	.open = vin_open,
	.release = vin_close,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = vin_compat_ioctl32,
#endif
	.mmap = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops vin_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap_mplane,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_g_fmt_vid_cap_mplane = vidioc_g_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_s_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_overlay = vidioc_enum_fmt_vid_overlay,
	.vidioc_g_fmt_vid_overlay = vidioc_g_fmt_vid_overlay,
	.vidioc_try_fmt_vid_overlay = vidioc_try_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay = vidioc_s_fmt_vid_overlay,
	.vidioc_overlay = vidioc_overlay,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_g_parm = vidioc_g_parm,
	.vidioc_s_parm = vidioc_s_parm,
	.vidioc_g_crop = vidioc_g_crop,
	.vidioc_s_crop = vidioc_s_crop,
	.vidioc_g_selection = vidioc_g_selection,
	.vidioc_s_selection = vidioc_s_selection,
	.vidioc_default = vin_param_handler,
	.vidioc_subscribe_event = vin_subscribe_event,
	.vidioc_unsubscribe_event = vin_unsubscribe_event,
};

static const struct v4l2_ctrl_config ae_win_ctrls[] = {
	{
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AE_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
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
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AF_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
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

static const struct v4l2_ctrl_config custom_ctrls[] = {
	{
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_HOR_VISUAL_ANGLE,
		.name = "Horizontal Visual Angle",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 360,
		.step = 1,
		.def = 60,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_VER_VISUAL_ANGLE,
		.name = "Vertical Visual Angle",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 360,
		.step = 1,
		.def = 60,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_FOCUS_LENGTH,
		.name = "Focus Length",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 1000,
		.step = 1,
		.def = 280,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_INIT,
		.name = "AutoFocus Initial",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_RELEASE,
		.name = "AutoFocus Release",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_GSENSOR_ROTATION,
		.name = "Gsensor Rotaion",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = -180,
		.max = 180,
		.step = 90,
		.def = 0,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_TAKE_PICTURE,
		.name = "Take Picture",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 16,
		.step = 1,
		.def = 0,
	}, {
		.ops = &vin_ctrl_ops,
		.id = V4L2_CID_SENSOR_TYPE,
		.name = "Sensor type",
		.type = V4L2_CTRL_TYPE_MENU,
		.min = 0,
		.max = 1,
		.def = 0,
		.menu_skip_mask = 0x0,
		.qmenu = sensor_info_type,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
};
static const s64 iso_qmenu[] = {
	50, 100, 200, 400, 800,
};
static const s64 exp_bias_qmenu[] = {
	-4, -3, -2, -1, 0, 1, 2, 3, 4,
};

int vin_init_controls(struct v4l2_ctrl_handler *hdl, struct vin_vid_cap *cap)
{
	struct v4l2_ctrl *ctrl;
	unsigned int i, ret = 0;

	v4l2_ctrl_handler_init(hdl, 37 + ARRAY_SIZE(custom_ctrls)
		+ ARRAY_SIZE(ae_win_ctrls) + ARRAY_SIZE(af_win_ctrls));
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_BRIGHTNESS, 0, 255, 1,
			  128);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_CONTRAST, 0, 128, 1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_SATURATION, -4, 4, 1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_HUE, -180, 180, 1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_WHITE_BALANCE, 0, 1,
			  1, 1);
	ctrl =
	    v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_EXPOSURE, 0,
			      65536 * 16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	ctrl =
	    v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_GAIN, 1 * 16,
			      64 * 16 - 1, 1, 1 * 16);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops,
			       V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_HUE_AUTO, 0, 1, 1, 1);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops,
			  V4L2_CID_WHITE_BALANCE_TEMPERATURE, 2800, 10000, 1,
			  6500);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_SHARPNESS, -32, 32, 1,
			  0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_CHROMA_AGC, 0, 1, 1, 1);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops, V4L2_CID_COLORFX,
			       V4L2_COLORFX_SET_CBCR, 0, V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTOBRIGHTNESS, 0, 1, 1,
			  1);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_BAND_STOP_FILTER, 0, 1,
			  1, 1);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_ILLUMINATORS_1, 0, 1, 1,
			  0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_ILLUMINATORS_2, 0, 1, 1,
			  0);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops, V4L2_CID_EXPOSURE_AUTO,
			       V4L2_EXPOSURE_APERTURE_PRIORITY, 0,
			       V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_EXPOSURE_ABSOLUTE, 1,
			  1000000, 1, 1);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_EXPOSURE_AUTO_PRIORITY,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_FOCUS_ABSOLUTE, 0, 127,
			  1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_FOCUS_RELATIVE, -127,
			  127, 1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_FOCUS_AUTO, 0, 1, 1, 1);
	v4l2_ctrl_new_int_menu(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_EXPOSURE_BIAS,
			       ARRAY_SIZE(exp_bias_qmenu) - 1,
			       ARRAY_SIZE(exp_bias_qmenu) / 2, exp_bias_qmenu);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops,
			       V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			       V4L2_WHITE_BALANCE_SHADE, 0,
			       V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1,
			  1, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_IMAGE_STABILIZATION, 0,
			  1, 1, 0);
	v4l2_ctrl_new_int_menu(hdl, &vin_ctrl_ops, V4L2_CID_ISO_SENSITIVITY,
			       ARRAY_SIZE(iso_qmenu) - 1,
			       ARRAY_SIZE(iso_qmenu) / 2 - 1, iso_qmenu);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops,
			       V4L2_CID_ISO_SENSITIVITY_AUTO,
			       V4L2_ISO_SENSITIVITY_AUTO, 0,
			       V4L2_ISO_SENSITIVITY_AUTO);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops, V4L2_CID_SCENE_MODE,
			       V4L2_SCENE_MODE_TEXT, 0, V4L2_SCENE_MODE_NONE);
	ctrl =
	    v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_3A_LOCK, 0, 7, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_FOCUS_START, 0, 0,
			  0, 0);
	v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_FOCUS_STOP, 0, 0, 0,
			  0);
	ctrl =
	    v4l2_ctrl_new_std(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_FOCUS_STATUS, 0,
			      7, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops, V4L2_CID_AUTO_FOCUS_RANGE,
			       V4L2_AUTO_FOCUS_RANGE_INFINITY, 0,
			       V4L2_AUTO_FOCUS_RANGE_AUTO);
	v4l2_ctrl_new_std_menu(hdl, &vin_ctrl_ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_RED_EYE, 0,
			       V4L2_FLASH_LED_MODE_NONE);

	for (i = 0; i < ARRAY_SIZE(custom_ctrls); i++)
		v4l2_ctrl_new_custom(hdl, &custom_ctrls[i], NULL);

	for (i = 0; i < ARRAY_SIZE(ae_win_ctrls); i++)
		cap->ae_win[i] = v4l2_ctrl_new_custom(hdl,
						&ae_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(ae_win_ctrls), &cap->ae_win[0]);

	for (i = 0; i < ARRAY_SIZE(af_win_ctrls); i++)
		cap->af_win[i] = v4l2_ctrl_new_custom(hdl,
						&af_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(af_win_ctrls), &cap->af_win[0]);

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
	}
	return ret;
}

static struct video_device vin_template[] = {
	[0] = {
	       .name = "vin_video0",
	       .fops = &vin_fops,
	       .ioctl_ops = &vin_ioctl_ops,
	       .release = video_device_release_empty,
	       },
	[1] = {
	       .name = "vin_video1",
	       .fops = &vin_fops,
	       .ioctl_ops = &vin_ioctl_ops,
	       .release = video_device_release_empty,
	       },
};

int vin_init_video(struct v4l2_device *v4l2_dev, struct vin_vid_cap *cap)
{
	int ret = 0;
	struct vb2_queue *q;
	static u64 vin_dma_mask = DMA_BIT_MASK(32);

	cap->vdev = vin_template[cap->vinc->id];
	cap->vdev.ctrl_handler = &cap->ctrl_handler;
	cap->vdev.v4l2_dev = v4l2_dev;
	cap->vdev.queue = &cap->vb_vidq;
	cap->vdev.lock = &cap->lock;
	cap->vdev.flags = V4L2_FL_USES_V4L2_FH;
	ret = video_register_device(&cap->vdev, VFL_TYPE_GRABBER, cap->vinc->id);
	if (ret < 0) {
		vin_err("Error video_register_device!!\n");
		return -1;
	}
	video_set_drvdata(&cap->vdev, cap->vinc);
	vin_print("V4L2 device registered as %s\n",
		video_device_node_name(&cap->vdev));

	/* Initialize videobuf2 queue as per the buffer type */
	cap->vinc->pdev->dev.dma_mask = &vin_dma_mask;
	cap->vinc->pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	cap->alloc_ctx = vb2_dma_contig_init_ctx(&cap->vinc->pdev->dev);
	if (IS_ERR(cap->alloc_ctx)) {
		vin_err("Failed to get the context\n");
		return -1;
	}
	/* initialize queue */
	q = &cap->vb_vidq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_READ;
	q->drv_priv = cap;
	q->buf_struct_size = sizeof(struct vin_buffer);
	q->ops = &vin_video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &cap->lock;

	ret = vb2_queue_init(q);
	if (ret) {
		vin_err("vb2_queue_init() failed\n");
		vb2_dma_contig_cleanup_ctx(cap->alloc_ctx);
		return ret;
	}

	cap->vd_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&cap->vdev.entity, 1, &cap->vd_pad, 0);
	if (ret)
		return ret;

	INIT_WORK(&cap->s_stream_task, __vin_s_stream_handle);

	cap->state = 0;
	cap->registered = 1;
	/* initial state */
	cap->capture_mode = V4L2_MODE_PREVIEW;
	/* init video dma queues */
	INIT_LIST_HEAD(&cap->vidq_active);
	mutex_init(&cap->lock);
	spin_lock_init(&cap->slock);

	return 0;
}

static int vin_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations vin_sd_media_ops = {
	.link_setup = vin_link_setup,
};

static int vin_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_fh *fh,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	return 0;
}

static int vin_subdev_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	return 0;
}

static int vin_subdev_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	return 0;
}

static int vin_subdev_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_selection *sel)
{
	return 0;
}

static int vin_subdev_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_selection *sel)
{
	return 0;
}
static int vin_video_core_s_power(struct v4l2_subdev *sd, int on)
{
	struct vin_core *vinc = v4l2_get_subdevdata(sd);
	if (on)
		pm_runtime_get_sync(&vinc->pdev->dev);/*call pm_runtime resume*/
	else
		pm_runtime_put_sync(&vinc->pdev->dev);/*call pm_runtime suspend*/
	return 0;
}

static int vin_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct vin_core *vinc = v4l2_get_subdevdata(sd);
	struct vin_vid_cap *cap = &vinc->vid_cap;
	struct csic_dma_cfg cfg;
	struct csic_dma_flip flip;
	struct dma_output_size size;
	struct dma_buf_len buf_len;
	struct dma_flip_size flip_size;
	int flag = 0;

	vin_print("%s on = %d, %d*%d hstart = %d vstart = %d\n", __func__, enable,
		cap->frame.o_width, cap->frame.o_height,
		cap->frame.offs_h, cap->frame.offs_v);

	if (enable) {
		memset(&cfg, 0, sizeof(cfg));
		memset(&size, 0, sizeof(size));
		memset(&buf_len, 0, sizeof(buf_len));

		switch (cap->frame.fmt->field) {
		case V4L2_FIELD_ANY:
		case V4L2_FIELD_NONE:
			cfg.field = FIELD_EITHER;
			break;
		case V4L2_FIELD_TOP:
			cfg.field = FIELD_1;
			flag = 1;
			break;
		case V4L2_FIELD_BOTTOM:
			cfg.field = FIELD_2;
			flag = 1;
			break;
		case V4L2_FIELD_INTERLACED:
			cfg.field = FIELD_EITHER;
			flag = 1;
			break;
		default:
			cfg.field = FIELD_EITHER;
			break;
		}

		switch (cap->frame.fmt->fourcc) {
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV12M:
			cfg.fmt = flag ? FRAME_UV_CB_YUV420 : FIELD_UV_CB_YUV420;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV21M:
			cfg.fmt = flag ? FRAME_VU_CB_YUV420 : FIELD_VU_CB_YUV420;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YUV420M:
			cfg.fmt = flag ? FRAME_PLANAR_YUV420 : FIELD_PLANAR_YUV420;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y >> 1;
			break;
		case V4L2_PIX_FMT_NV61:
			cfg.fmt = flag ? FRAME_UV_CB_YUV422 : FIELD_UV_CB_YUV422;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_NV16:
			cfg.fmt = flag ? FRAME_VU_CB_YUV422 : FIELD_VU_CB_YUV422;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_SBGGR8:
		case V4L2_PIX_FMT_SGBRG8:
		case V4L2_PIX_FMT_SGRBG8:
		case V4L2_PIX_FMT_SRGGB8:
			cfg.fmt = flag ? FRAME_RAW_8 : FIELD_RAW_8;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_SBGGR10:
		case V4L2_PIX_FMT_SGBRG10:
		case V4L2_PIX_FMT_SGRBG10:
		case V4L2_PIX_FMT_SRGGB10:
			cfg.fmt = flag ? FRAME_RAW_10 : FIELD_RAW_10;
			buf_len.buf_len_y = cap->frame.o_width * 2;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		case V4L2_PIX_FMT_SBGGR12:
		case V4L2_PIX_FMT_SGBRG12:
		case V4L2_PIX_FMT_SGRBG12:
		case V4L2_PIX_FMT_SRGGB12:
			cfg.fmt = flag ? FRAME_RAW_12 : FIELD_RAW_12;
			buf_len.buf_len_y = cap->frame.o_width * 2;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		default:
			cfg.fmt = flag ? FRAME_UV_CB_YUV420 : FIELD_UV_CB_YUV420;
			buf_len.buf_len_y = cap->frame.o_width;
			buf_len.buf_len_c = buf_len.buf_len_y;
			break;
		}

		csic_dma_config(vinc->vipp_sel, &cfg);

		size.hor_len = cap->frame.o_width;
		size.ver_len = cap->frame.o_height;
		size.hor_start = cap->frame.offs_h;
		size.ver_start = cap->frame.offs_v;
		flip_size.hor_len = size.hor_len * 2;
		flip_size.ver_len = size.ver_len;
		flip.hflip_en = vinc->hflip;
		flip.vflip_en = vinc->vflip;
		csic_dma_output_size_cfg(vinc->vipp_sel, &size);
		csic_dma_buffer_length(vinc->vipp_sel, &buf_len);
		csic_dma_flip_size(vinc->vipp_sel, &flip_size);
		csic_dma_flip_en(vinc->vipp_sel, &flip);
		csic_dma_enable(vinc->vipp_sel);
		csic_dma_int_clear_status(vinc->vipp_sel, DMA_INT_ALL);
		csic_dma_int_enable(vinc->vipp_sel, DMA_INT_CAPTURE_DONE |
					DMA_INT_FRAME_DONE |
					DMA_INT_BUF_0_OVERFLOW |
					DMA_INT_BUF_1_OVERFLOW |
					DMA_INT_BUF_2_OVERFLOW |
					DMA_INT_HBLANK_OVERFLOW);
	} else {
		csic_dma_int_disable(vinc->vipp_sel, DMA_INT_ALL);
		csic_dma_int_clear_status(vinc->vipp_sel, DMA_INT_ALL);
		csic_dma_disable(vinc->vipp_sel);
	}

	return 0;
}

static struct v4l2_subdev_core_ops vin_subdev_core_ops = {
	.s_power = vin_video_core_s_power,
};

static const struct v4l2_subdev_video_ops vin_subdev_video_ops = {
	.s_stream = vin_subdev_s_stream,
};

static struct v4l2_subdev_pad_ops vin_subdev_pad_ops = {
	.enum_mbus_code = vin_subdev_enum_mbus_code,
	.get_selection = vin_subdev_get_selection,
	.set_selection = vin_subdev_set_selection,
	.get_fmt = vin_subdev_get_fmt,
	.set_fmt = vin_subdev_set_fmt,
};

static struct v4l2_subdev_ops vin_subdev_ops = {
	.core = &vin_subdev_core_ops,
	.video = &vin_subdev_video_ops,
	.pad = &vin_subdev_pad_ops,
};

static int vin_capture_subdev_registered(struct v4l2_subdev *sd)
{
	struct vin_core *vinc = v4l2_get_subdevdata(sd);
	int ret;
	vin_print("vin video subdev registered\n");
	vinc->vid_cap.vinc = vinc;
	if (vin_init_controls(&vinc->vid_cap.ctrl_handler, &vinc->vid_cap)) {
		vin_err("Error v4l2 ctrls new!!\n");
		return -1;
	}

	vinc->pipeline_ops = v4l2_get_subdev_hostdata(sd);
	if (vin_init_video(sd->v4l2_dev, &vinc->vid_cap)) {
		vin_err("vin init video!!!!\n");
		vinc->pipeline_ops = NULL;
	}
	ret = sysfs_create_link(&vinc->vid_cap.vdev.dev.kobj,
		&vinc->pdev->dev.kobj, "vin_dbg");
	if (ret)
		vin_err("sysfs_create_link failed\n");

	return 0;
}

static void vin_capture_subdev_unregistered(struct v4l2_subdev *sd)
{
	struct vin_core *vinc = v4l2_get_subdevdata(sd);

	if (vinc == NULL)
		return;

	if (video_is_registered(&vinc->vid_cap.vdev)) {
		sysfs_remove_link(&vinc->vid_cap.vdev.dev.kobj, "vin_dbg");
		vin_print("unregistering %s\n",
			video_device_node_name(&vinc->vid_cap.vdev));
		video_unregister_device(&vinc->vid_cap.vdev);
		vb2_dma_contig_cleanup_ctx(vinc->vid_cap.alloc_ctx);
		media_entity_cleanup(&vinc->vid_cap.vdev.entity);
		flush_work(&vinc->vid_cap.s_stream_task);
	}
	v4l2_ctrl_handler_free(&vinc->vid_cap.ctrl_handler);
	vinc->pipeline_ops = NULL;
}

static const struct v4l2_subdev_internal_ops vin_capture_sd_internal_ops = {
	.registered = vin_capture_subdev_registered,
	.unregistered = vin_capture_subdev_unregistered,
};

int vin_initialize_capture_subdev(struct vin_core *vinc)
{
	struct v4l2_subdev *sd = &vinc->vid_cap.subdev;
	int ret;

	v4l2_subdev_init(sd, &vin_subdev_ops);
	sd->grp_id = VIN_GRP_ID_CAPTURE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "vin_cap.%d", vinc->id);

	vinc->vid_cap.sd_pads[VIN_SD_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	vinc->vid_cap.sd_pads[VIN_SD_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	ret = media_entity_init(&sd->entity, VIN_SD_PADS_NUM,
				vinc->vid_cap.sd_pads, 0);
	if (ret)
		return ret;

	sd->entity.ops = &vin_sd_media_ops;
	sd->internal_ops = &vin_capture_sd_internal_ops;
	v4l2_set_subdevdata(sd, vinc);
	return 0;
}

void vin_cleanup_capture_subdev(struct vin_core *vinc)
{
	struct v4l2_subdev *sd = &vinc->vid_cap.subdev;

	media_entity_cleanup(&sd->entity);
	v4l2_set_subdevdata(sd, NULL);
}

