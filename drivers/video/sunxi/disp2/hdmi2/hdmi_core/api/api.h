/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef API_H_
#define API_H_
#include "log.h"
#include "general_ops.h"

#include "core/main_controller.h"
#include "core/video.h"
#include "core/audio.h"
#include "core/packets.h"
#include "core/irq.h"

#include "hdcp.h"

#include "core/fc_video.h"
#include "edid.h"

#include "access.h"

#include "phy.h"
#include "scdc.h"

#include "hdmitx_dev.h"
#include "identification.h"


void resistor_calibration(hdmi_tx_dev_t *dev, u32 reg, u32 data);

void hdmitx_api_init(hdmi_tx_dev_t *dev, char *name);

int api_audio_mute(hdmi_tx_dev_t *dev, u8 state);
int api_audio_configure(hdmi_tx_dev_t *dev, audioParams_t *audio);

/**
 * Configure API.
 * Configure the modules of the API according to the parameters given by
 * the user. If EDID at sink is read, it does parameter checking using the
 * Check methods against the sink's E-EDID. Violations are outputted to the
 * buffer.
 * Shall only be called after an Init call or configure.
 * @param video parameters pointer
 * @param audio parameters pointer
 * @param product parameters pointer
 * @param hdcp parameters pointer
 * @return TRUE when successful
 * @note during this function, all controller's interrupts are disabled
 * @note this function needs to have the HW initialized before the first call
 */
int api_Configure(hdmi_tx_dev_t *dev, videoParams_t *video,
			audioParams_t *audio, productParams_t *product,
				hdcpParams_t *hdcp, u16 phy_model);

/**
 * Prepare API modules and local variables to standby mode
 *(hdmi_tx_dev_t *dev, and not respond
 * to interrupts) and frees all resources
 * @return TRUE when successful
 * @note must be called to free up resources and before another Init.
 */
int api_Standby(hdmi_tx_dev_t *dev);

#endif	/* API_H_ */

