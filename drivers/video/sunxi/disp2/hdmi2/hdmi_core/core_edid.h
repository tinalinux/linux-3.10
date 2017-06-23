/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef INCLUDE_APP_EDID_EDID_H_
#define INCLUDE_APP_EDID_EDID_H_

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/ioctl.h>

#include "api/hdmitx_dev.h"
#include "api/core/video.h"
#include "api/core/audio.h"
#include "api/general_ops.h"

typedef struct {

	/**
	 * Array to hold all the parsed Detailed Timing Descriptors.
	 */
	dtd_t edid_mDtd[32];

	unsigned int edid_mDtdIndex;
	/**
	 * array to hold all the parsed Short Video Descriptors.
	 */
	shortVideoDesc_t edid_mSvd[128];

	/* shortVideoDesc_t tmpSvd; */

	unsigned int edid_mSvdIndex;
	/**
	 * array to hold all the parsed Short Audio Descriptors.
	 */
	shortAudioDesc_t edid_mSad[128];

	unsigned int edid_mSadIndex;

	/**
	 * A string to hold the Monitor Name parsed from EDID.
	 */
	char edid_mMonitorName[13];

	int edid_mYcc444Support;

	int edid_mYcc422Support;

	int edid_mYcc420Support;

	int edid_mBasicAudioSupport;

	int edid_mUnderscanSupport;

	/**
	 *  If Sink is HDMI 2.0
	 */
	int edid_m20Sink;

	hdmivsdb_t edid_mHdmivsdb;

	hdmiforumvsdb_t edid_mHdmiForumvsdb;

	monitorRangeLimits_t edid_mMonitorRangeLimits;

	videoCapabilityDataBlock_t edid_mVideoCapabilityDataBlock;

	colorimetryDataBlock_t edid_mColorimetryDataBlock;

	struct hdr_static_metadata_data_block edid_hdr_static_metadata_data_block;

	speakerAllocationDataBlock_t edid_mSpeakerAllocationDataBlock;
} sink_edid_t;


extern int edid_read(hdmi_tx_dev_t *dev, struct edid *edid);
extern int edid_extension_read(hdmi_tx_dev_t *dev, int block, u8 *edid_ext);

void edid_read_cap(void);

/* int edid_read_hdmitx_cap(char * edid_tx_cap_file); */

int edid_tx_supports_cea_code(u32 cea_code);


void edid_set_video_prefered(hdmi_tx_dev_t *dev, sink_edid_t *sink_cap,
							videoParams_t *pVideo);

void edid_set_audio_prefered(hdmi_tx_dev_t *dev, sink_edid_t *sink_cap,
							audioParams_t *pAudio);

int edid_sink_supports_vic_code(u32 vic_code);

#endif /* INCLUDE_APP_EDID_EDID_H_ */
