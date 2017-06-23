/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


#ifndef PLATFORM_H_
#define PLATFORM_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sys_config.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <video/sunxi_display2.h>
#include <video/drv_hdmi.h>

#include "core_edid.h"
#include "api/hdmitx_dev.h"
#include "api/core/irq.h"
#include "api/general_ops.h"
#include "api/core/audio.h"


enum hpd_state_value {
	HDMI_State_Idle = 0,
	HDMI_State_Hdcp_Irq,
	HDMI_State_Wait_Hpd,
	HDMI_State_Rx_Sense,
	HDMI_State_EDID_Parse,
	HDMI_State_HPD_Done
};

enum irq_type {
	NONE_IRQ = 0,
	HDCP_IRQ = 1,
	HPD_IRQ
};

struct hdmi_mode {
	videoParams_t		pVideo;
	audioParams_t		pAudio;
	hdcpParams_t		pHdcp;
	productParams_t	pProduct;

	/* productParams_t	mProduct; */
	/* hdmivsdb_t		vsdb; */
	/* hdmiforumvsdb_t	forumvsdb; */

	/* uint8_t			ksv_list_buffer[670]; */
	uint8_t			ksv_devices;
	uint8_t			dpk_aksv[7];
	uint8_t			sw_enc_key[2];
	uint8_t			dpk_keys[560];

	int			edid_done;

	struct edid	      *edid;
	u8		      *edid_ext; /*edid extenssion raw data*/
	sink_edid_t	      *sink_cap;
};


/****************video******************************/
#define HDMI1440_480I           6
#define HDMI1440_576I           21
#define HDMI480P                        2
#define HDMI576P                        17
#define HDMI720P_50                     19
#define HDMI720P_60             4
#define HDMI1080I_50            20
#define HDMI1080I_60            5
#define HDMI1080P_50            31
#define HDMI1080P_60            16
#define HDMI1080P_24            32
#define HDMI1080P_25            33
#define HDMI1080P_30            34
#define HDMI1080P_24_3D_FP  (HDMI1080P_24 + 0x80)
#define HDMI720P_50_3D_FP   (HDMI720P_50 + 0x80)
#define HDMI720P_60_3D_FP   (HDMI720P_60 + 0x80)
#define HDMI3840_2160P_30   95
#define HDMI3840_2160P_25   94
#define HDMI3840_2160P_24   93
#define HDMI4096_2160P_24   98
#define HDMI4096_2160P_25   99
#define HDMI4096_2160P_30   100


#define HDMI3840_2160P_60   97
#define HDMI4096_2160P_60   102

struct disp_hdmi_mode {
	enum disp_tv_mode mode;
	int hdmi_mode;/* vic */
};

struct api_function {
	int (*phy_write)(hdmi_tx_dev_t *dev, u8 addr, u16 data);
	int (*phy_read)(hdmi_tx_dev_t *dev, u8 addr, u16 *value);

	int (*scdc_write)(hdmi_tx_dev_t *dev, u8 address, u8 size, u8 *data);
	int (*scdc_read)(hdmi_tx_dev_t *dev, u8 address, u8 size, u8 *data);
};
/**
 * Main structure
 */
struct hdmi_tx_core {
	enum hpd_state_value		hpd_state;

	/** PHY version */
	int						hdmi_tx_phy;

	/* Reserved for API internal use only */
	/** HDMI TX API Internals */
	struct device_access		dev_access;
	struct system_functions		sys_functions;

	/*for hdmi api*/
	hdmi_tx_dev_t				hdmi_tx;

	/** Application mode configurations */
	struct hdmi_mode			mode;
	/** Configurations to corely */
	struct hdmi_mode			*user_cfg;

	struct api_function			api_func;
};


extern u32 hdmi_hpd_mask;

int hdmi_tx_core_init(struct hdmi_tx_core *core, int phy);
void hdmi_core_exit(struct hdmi_tx_core *core);

void resistor_calibration_core(struct hdmi_tx_core *core, u32 reg, u32 data);

/****************************IRQ handler********************************/
void hdcp_handler_core(void);
u32 hdmi_core_get_irq_type(void);

void hpd_handler_core(void);


void hdmi_core_set_base_addr(uintptr_t reg_base);
uintptr_t hdmi_core_get_base_addr(void);

void set_platform(struct hdmi_tx_core *core);
struct hdmi_tx_core *get_platform(void);

u32 hdmi_core_get_hpd_state(void);


/********************video******************************/
extern dtd_t *get_dtd(u8 code, u32 refreshRate);
s32 set_static_config(struct disp_device_config *config);
s32 get_static_config(struct disp_device_config *config);
s32 set_dynamic_config(struct disp_device_dynamic_config *config);
s32 get_dynamic_config(struct disp_device_dynamic_config *config);
s32 hdmi_set_display_mode(u32 mode);
s32 hdmi_mode_support(u32 mode);
s32 hdmi_get_HPD_status(void);
s32 hdmi_core_get_csc_type(void);
s32 hdmi_get_video_timming_info(struct disp_video_timings **video_info);
s32 hdmi_enable_core(void);
s32 hdmi_disable_core(void);
s32 hdmi_suspend_core(void);
s32 hdmi_resume_core(void);


/*******************HDCP*****************************/
void hdcp_core_enable(u8 enable);

/*************************Audio***************************/
void coding_type_user_config(u32 coding_type);

void sample_freq_user_config(u32 freq);

void interface_type_user_config(u32 type);

void sample_size_user_config(u32 size);

void fs_factor_user_config(u32 fs_factor);

void audio_show_user_config(void);

void update_audio_cfg(audioParams_t *user, audioParams_t *cfg);

void audio_corely(void);


/*************************audio***************************/
#if defined(CONFIG_SND_SUNXI_SOC_HDMIAUDIO)
s32 hdmi_set_audio_para(hdmi_audio_t *audio_para);
s32 hdmi_core_audio_enable(u8 mode, u8 channel);
#endif

#endif /* PLATFORM_H_ */
