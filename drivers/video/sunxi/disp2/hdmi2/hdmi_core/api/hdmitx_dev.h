/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef INCLUDE_HDMITX_DEV_H_
#define INCLUDE_HDMITX_DEV_H_

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

/** event_t events to register a callback for in the API
 */
typedef enum {
	MODE_UNDEFINED = -1,
	DVI = 0,
	HDMI
} video_mode_t;

typedef enum {
	PHY_ACCESS_UNDEFINED = 0,
	PHY_I2C = 1,
	PHY_JTAG
} phy_access_t;

/**
 * @short HDMI TX controller status information
 *
 * Initialize @b user fields (set status to zero).
 * After opening this data is for internal use only.
 */
struct hdmi_tx_ctrl {
	/** (@b user) Context status: closed (0), opened (<0) and
	 *  configured (>0) */
	u8 data_enable_polarity;

	/** This is used to check if a cable is connected and if so the
	 * assume green light to configure the the core */
	int hpd;

	u32 pixel_clock;
	u8 pixel_repetition;
	u32 tmds_clk;
	u8 color_resolution;
	u8 csc_on; /*csc_on=1,color space clock will be enabled*/
	u8 audio_on;
	u8 cec_on;
	u8 hdcp_on;
	u8 hdmi_on;
	phy_access_t phy_access;
};

/**
 * @short Main structures to instantiate the driver
 */
struct hdmi_tx_phy {
	int version;

	int generation;
	/** (@b user) Context status: closed (0), opened (<0) and configured
	 *  (>0) */
	int status;
};

typedef struct {
	/** Bypass encryption */
	int bypass;

	/** Enable Feature 1.1 */
	int mEnable11Feature;

	/** Check Ri every 128th frame */
	int mRiCheck;

	/** I2C fast mode */
	int mI2cFastMode;

	/** Enhanced link verification */
	int mEnhancedLinkVerification;

	/** Number of supported devices
	 * (depending on instantiated KSV MEM RAM – Revocation Memory to support
	 * HDCP repeaters)
	 */
	u8 maxDevices;

	/** KSV List buffer
	 * Shall be dimensioned to accommodate 5[bytes] x No.
	 * of supported devices
	 * (depending on instantiated KSV MEM RAM – Revocation Memory to support
	 * HDCP repeaters)
	 * plus 8 bytes (64-bit) M0 secret value
	 * plus 2 bytes Bstatus
	 * Plus 20 bytes to calculate the SHA-1 (VH0-VH4)
	 * Total is (30[bytes] + 5[bytes] x Number of supported devices)
	 */
	u8 *mKsvListBuffer;

	/** aksv total of 14 chars**/
	u8 *mAksv;

	/** Keys list
	 * 40 Keys of 14 characters each
	 * stored in segments of 8 bits (2 chars)
	 * **/
	u8 *mKeys;

	u8 *mSwEncKey;
} hdcpParams_t;

/**
 * @short Main structures to instantiate the driver
 */
typedef struct hdmi_tx_dev_api {
	char					device_name[20];

	/** SYNOPSYS DATA */
	struct hdmi_tx_ctrl	snps_hdmi_ctrl;

	hdcpParams_t			hdcp;
} hdmi_tx_dev_t;


#endif /* INCLUDE_HDMITX_DEV_H_ */
