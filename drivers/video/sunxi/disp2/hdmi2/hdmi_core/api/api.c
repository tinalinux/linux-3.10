/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "api.h"

void resistor_calibration(hdmi_tx_dev_t *dev, u32 reg, u32 data)
{
	dev_write(dev, reg * 4, data);
	dev_write(dev, (reg + 1) * 4, data >> 8);
	dev_write(dev, (reg + 2) * 4, data >> 16);
	dev_write(dev, (reg + 3) * 4, data >> 24);
}

void hdmitx_api_init(hdmi_tx_dev_t *dev, char *name)
{
	memset(dev, 0, sizeof(hdmi_tx_dev_t));
	memcpy(dev->device_name, name, sizeof(dev->device_name));
}

static void api_set_hdmi_ctrl(hdmi_tx_dev_t *dev, videoParams_t *video,
							hdcpParams_t *hdcp)
{
	video_mode_t hdmi_on = 0;
	struct hdmi_tx_ctrl *tx_ctrl = &dev->snps_hdmi_ctrl;

	hdmi_on = video->mHdmi;
	tx_ctrl->hdmi_on = (hdmi_on == HDMI) ? 1 : 0;
	tx_ctrl->pixel_clock = videoParams_GetPixelClock(dev, video);
	tx_ctrl->color_resolution = video->mColorResolution;
	tx_ctrl->pixel_repetition = video->mDtd.mPixelRepetitionInput;
	tx_ctrl->csc_on = 1;
	tx_ctrl->audio_on = (hdmi_on == HDMI) ? 1 : 0;
}

int api_Standby(hdmi_tx_dev_t *dev)
{
	mc_disable_all_clocks(dev);
	phy_standby(dev);

	dev->snps_hdmi_ctrl.hdmi_on = 0;
	dev->snps_hdmi_ctrl.pixel_clock = 0;
	dev->snps_hdmi_ctrl.color_resolution = 0;
	dev->snps_hdmi_ctrl.pixel_repetition = 0;
	dev->snps_hdmi_ctrl.csc_on = 0;
	dev->snps_hdmi_ctrl.audio_on = 0;

	return true;
}

static void api_avmute(hdmi_tx_dev_t *dev, int enable)
{
	packets_AvMute(dev, enable);
	hdcp_av_mute(dev, enable);
}

int api_audio_mute(hdmi_tx_dev_t *dev, u8 state)
{
	int ret = true;

	ret = audio_mute(dev, state);

	return ret;

}

int api_audio_configure(hdmi_tx_dev_t *dev, audioParams_t *audio)
{
	int success = true;

	/* Audio - Workaround */
	audio_Initialize(dev);
	success = audio_Configure(dev, audio);
	if (success == false)
		HDMI_INFO_MSG("ERROR:Audio not configured\n");

	return success;
}

int api_Configure(hdmi_tx_dev_t *dev, videoParams_t *video,
				audioParams_t *audio, productParams_t *product,
				hdcpParams_t *hdcp, u16 phy_model)
{
	int success = true;
	unsigned int tmds_clk = 0;

	LOG_TRACE();
	if (!dev) {
		HDMI_INFO_MSG("ERROR:dev pointer is NULL\n");
		return false;
	}

	if (!video || !audio || !product || !hdcp) {
		HDMI_INFO_MSG("ERROR:There is NULL value arguments: video=%lx; audio=%lx; product=%lx; hdcp=%lx\n",
					(uintptr_t)video, (uintptr_t)audio,
					(uintptr_t)product, (uintptr_t)hdcp);
		return false;
	}

	if (dev->snps_hdmi_ctrl.hpd == 0)
		HDMI_INFO_MSG("WARN:Trying to configure HDMI TX without a sink\n");

	api_set_hdmi_ctrl(dev, video, hdcp);

	mc_rst_all_clocks(dev);

	api_avmute(dev, true);

	phy_standby(dev);

	/* Disable interrupts */
	irq_mute(dev);

	success = video_Configure(dev, video);
	if (success == false)
		HDMI_INFO_MSG("Could not configure video\n");

	/* Audio - Workaround */
	audio_Initialize(dev);
	success = audio_Configure(dev, audio);
	if (success == false)
		HDMI_INFO_MSG("ERROR:Audio not configured\n");


	/* Packets */
	success = packets_Configure(dev, video, product);
	if (success == false)
		HDMI_INFO_MSG("ERROR:Could not configure packets\n");

	mc_enable_all_clocks(dev);
	snps_sleep(10000);

	HDMI_INFO_MSG("video pixel clock=%d\n", dev->snps_hdmi_ctrl.pixel_clock);
	/*for auto scrambling if tmds_clk > 3.4Gbps*/
	switch (video->mColorResolution) {
	case COLOR_DEPTH_8:
		tmds_clk = dev->snps_hdmi_ctrl.pixel_clock;
		break;
	case COLOR_DEPTH_10:
		if (video->mEncodingOut != YCC422)
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock*125/100;
		else
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock;

		break;
	default:
		break;
	}
	dev->snps_hdmi_ctrl.tmds_clk = tmds_clk;
	if (dev->snps_hdmi_ctrl.tmds_clk  > 340000) {
		scrambling(dev, 1);
		HDMI_INFO_MSG("enable scrambling\n");
	} else {
		scrambling(dev, 0);
		HDMI_INFO_MSG("disable scrambling\n");
	}

	if (video->mEncodingIn == YCC420)
		dev->snps_hdmi_ctrl.pixel_clock = dev->snps_hdmi_ctrl.pixel_clock / 2;
	if (video->mEncodingIn == YCC422)
		dev->snps_hdmi_ctrl.color_resolution = 8;

	/*add calibrated resistor configuration for all video resolution*/
	dev_write(dev, 0x40018, 0xc0);
	dev_write(dev, 0x4001c, 0x80);

	success = phy_configure(dev, phy_model);
	if (success == false)
		HDMI_INFO_MSG("ERROR:Could not configure PHY\n");

	/* Disable blue screen transmission
		after turning on all necessary blocks (e.g. HDCP) */
	fc_force_output(dev, false);

	/* TODO:This should be called at HPD event */
	/* HDCP is PHY independent */
	if (hdcp_initialize(dev) != true)
		HDMI_INFO_MSG("ERROR:Could not initialize HDCP\n");

	snps_sleep(20000);
	success = hdcp_configure(dev, hdcp, video);
	if (success == false)
		HDMI_INFO_MSG("ERROR:Could not configure HDCP\n");

	irq_mask_all(dev);
	/* enable interrupts */
	irq_unmute(dev);

	api_avmute(dev, false);

	return success;
}
