
/*
 ******************************************************************************
 *
 * sunxi_isp.h
 *
 * Hawkview ISP - sunxi_isp.h module
 *
 * Copyright (c) 2014 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   3.0		  Yang Feng   	2014/12/11	ISP Tuning Tools Support
 *
 ******************************************************************************
 */

#ifndef _SUNXI_ISP_H_
#define _SUNXI_ISP_H_
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include "../vin-video/vin_core.h"
#include "bsp_isp.h"

#include "../vin-stat/vin_ispstat.h"
#include "../vin-stat/vin_h3a.h"

enum isp_pad {
	ISP_PAD_SINK,
	ISP_PAD_SOURCE_ST,
	ISP_PAD_SOURCE,
	ISP_PAD_NUM,
};

struct isp_pix_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	enum isp_input_seq infmt;
	char *name;
	u32 fourcc;
	u32 color;
	u16 memplanes;
	u16 colplanes;
	u32 depth[VIDEO_MAX_PLANES];
	u16 mdataplanes;
	u16 flags;
};

struct sunxi_isp_ctrls {
	struct v4l2_ctrl_handler handler;

	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *rotate;
	struct v4l2_ctrl *wb_gain[4];	/* wb gain cluster */
	struct v4l2_ctrl *ae_win[4];	/* wb win cluster */
	struct v4l2_ctrl *af_win[4];	/* af win cluster */
};

struct isp_fmt_cfg {
	struct isp_size ob_black_size;
	struct isp_size ob_valid_size;
	struct coor ob_start;
};

struct isp_stat_to_user {
	/* v4l2 drivers fill */
	void *ae_buf;
	void *af_buf;
	void *awb_buf;
	void *hist_buf;
	void *afs_buf;
	void *pltm_buf;
};

struct isp_dev {
	int use_cnt;
	struct v4l2_subdev subdev;
	struct media_pad isp_pads[ISP_PAD_NUM];
	struct v4l2_event event;
	struct platform_device *pdev;
	struct list_head isp_list;
	struct sunxi_isp_ctrls ctrls;
	int capture_mode;
	int use_isp;
	int is_raw;
	int irq;
	unsigned int id;
	spinlock_t slock;
	struct mutex subdev_lock;
	struct work_struct isp_isr_bh_task;
	wait_queue_head_t wait;
	void __iomem *base;
	struct vin_mm isp_load;
	struct vin_mm isp_save;
	struct vin_mm isp_lut_tbl;
	struct vin_mm isp_drc_tbl;
	struct isp_table_addr isp_tbl;
	struct isp_stat_to_user *stat_buf;
	struct isp_pix_fmt *isp_fmt;
	struct v4l2_mbus_framefmt mf;
	struct isp_stat h3a_stat;
};

void update_isp_setting(struct v4l2_subdev *sd);
int isp_table_request(struct v4l2_subdev *sd);
void isp_table_release(struct v4l2_subdev *sd);
void isp_isr_bh_handle(struct work_struct *work);

void sunxi_isp_sensor_type(struct v4l2_subdev *sd, int use_isp, int is_raw);
void sunxi_isp_dump_regs(struct v4l2_subdev *sd);
void sunxi_isp_vsync_isr(struct v4l2_subdev *sd);
void sunxi_isp_frame_sync_isr(struct v4l2_subdev *sd);
struct v4l2_subdev *sunxi_isp_get_subdev(int id);
struct v4l2_subdev *sunxi_stat_get_subdev(int id);
int sunxi_isp_platform_register(void);
void sunxi_isp_platform_unregister(void);

#endif /*_SUNXI_ISP_H_*/
