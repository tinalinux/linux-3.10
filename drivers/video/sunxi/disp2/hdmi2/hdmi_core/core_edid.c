/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


#include "core_edid.h"
#include "hdmi_core.h"

const unsigned DTD_SIZE = 0x12;

static int edid_parser(hdmi_tx_dev_t *dev, u8 *buffer, sink_edid_t *edidExt,
							u16 edid_size);
static int edid_parser_CeaExtReset(hdmi_tx_dev_t *dev, sink_edid_t *edidExt);
static int edid_parser_ParseDataBlock(hdmi_tx_dev_t *dev, u8 *data,
						sink_edid_t *edidExt);
static void edid_parser_updateYcc420(sink_edid_t *edidExt, u8 Ycc420All,
						u8 LimitedToYcc420All);



static void edid_init_sink_cap(sink_edid_t *sink)
{
	memset(sink, 0, sizeof(sink_edid_t));
}

void edid_read_cap(void)
{
	struct hdmi_tx_core *core = get_platform();
	struct edid  *edid = NULL;
	u8  *edid_ext = NULL;
	sink_edid_t *sink = NULL;
	int edid_ok = 0;
	int edid_tries = 3;

	LOG_TRACE();
	if (core == NULL) {
		HDMI_INFO_MSG("Could not get core structure\n");
		return;
	}

	core->mode.edid = NULL;

	/* Data allocation */
	edid = kzalloc(sizeof(struct edid), GFP_KERNEL);
	if (!edid) {
		HDMI_ERROR_MSG("Could not allocated hdmi_tx_core\n");
		return;
	}
	memset(edid, 0, sizeof(struct edid));

	edid_ext = kzalloc(128, GFP_KERNEL);
	if (!edid_ext) {
		HDMI_ERROR_MSG("Could not allocated edid_ext\n");
		return;
	}
	memset(edid_ext, 0, 128);

	sink = kzalloc(sizeof(sink_edid_t), GFP_KERNEL);
	if (!sink) {
		HDMI_ERROR_MSG("Could not allocated sink\n");
		return;
	}
	memset(sink, 0, sizeof(sink_edid_t));

	edid_init_sink_cap(sink);
	edid_parser_CeaExtReset(&core->hdmi_tx, sink);

	core->mode.sink_cap = NULL;


	do {
		edid_ok = edid_read(&core->hdmi_tx, edid);

		if (edid_ok) /* error case */
			continue;

		core->mode.edid = edid;

		if (edid_parser(&core->hdmi_tx,
			(u8 *) edid, sink, 128) == false) {
			HDMI_INFO_MSG("Could not parse EDID\n");
			core->mode.edid_done = 0;
			kfree(sink);
			return;
		}
		break;
	} while (edid_tries--);


	if (edid_tries <= 0) {
		HDMI_INFO_MSG("Could not read EDID\n");
		core->mode.edid_done = 0;
		kfree(sink);
		kfree(edid);
		return;
	}

	if (edid->extensions == 0) {
		core->mode.edid_done = 1;
	} else {
		int edid_ext_cnt = 1;

		while (edid_ext_cnt <= edid->extensions) {
			HDMI_INFO_MSG("EDID Extension %d\n", edid_ext_cnt);
			edid_tries = 3;
			do {
				edid_ok = edid_extension_read(&core->hdmi_tx,
							edid_ext_cnt, edid_ext);

				if (edid_ok)
					continue;

				core->mode.edid_ext = edid_ext;
		/* TODO: add support for EDID parsing w/ Ext blocks > 1 */
				if (edid_ext_cnt < 2) {
					if (edid_parser(&core->hdmi_tx,
							edid_ext,
							sink, 128) == false) {
						HDMI_INFO_MSG("Could not parse EDID EXTENSIONS\n");
						core->mode.edid_done = 0;
						/* free(sink); */
						/* return; */
					}
					core->mode.sink_cap = sink;
					core->mode.edid_done = 1;
				}
				break;
			} while (edid_tries--);
			edid_ext_cnt++;
		}
	}
}

/*Attention!!!!this function do not include 3D sink support*/
int edid_sink_supports_vic_code(u32 vic_code)
{
	int i = 0;
	struct hdmi_tx_core *p_core = get_platform();

	if (p_core == NULL)
		return false;


	if (p_core->mode.sink_cap == NULL)
		return false;


	for (i = 0; (i < 128) && (p_core->mode.sink_cap->edid_mSvd[i].mCode != 0); i++) {
		if (p_core->mode.sink_cap->edid_mSvd[i].mCode == vic_code)
			return true;
	}

	for (i = 0; (i < 4) && (p_core->mode.sink_cap->edid_mHdmivsdb.mHdmiVic[i] != 0); i++) {
		if (p_core->mode.sink_cap->edid_mHdmivsdb.mHdmiVic[i] == videoParams_GetHdmiVicCode(vic_code))
				return true;

	}

	if ((vic_code == 0x80 + 19) || (vic_code == 0x80 + 4)
		|| (vic_code == 0x80 + 32)) {
		if (p_core->mode.sink_cap->edid_mHdmivsdb.m3dPresent == 1)
			HDMI_INFO_MSG("Support basic 3d video format\n");
		else
			HDMI_INFO_MSG("Do not support any basic 3d video format\n");

	}

	return false;
}


void edid_set_video_prefered(hdmi_tx_dev_t *dev, sink_edid_t *sink_cap,
				videoParams_t *pVideo)
{
	/* Configure using Native SVD or HDMI_VIC */
	int i = 0;
	int cea_vic = 0;
	unsigned int vic_index = 0;

	LOG_TRACE();
	if ((dev == NULL) || (sink_cap == NULL) || (pVideo == NULL)) {
		HDMI_ERROR_MSG("Have NULL params\n");
		return;
	}

	for (i = 0; (i < 128) && (sink_cap->edid_mSvd[i].mCode != 0); i++) {
		if (sink_cap->edid_mSvd[i].mCode == pVideo->mDtd.mCode) {
			cea_vic = sink_cap->edid_mSvd[i].mCode;
			vic_index = i;
			break;
		}
	}

	pVideo->mHdmi20 = sink_cap->edid_m20Sink;

	dev_write_mask(dev, (0x1032<<2), 0xff, 0);
	dev_write_mask(dev, (0x1033<<2), 0xff, 0);

	videoParams_SetYcc420Support(dev, &pVideo->mDtd,
					&sink_cap->edid_mSvd[vic_index]);

}

void edid_set_audio_prefered(hdmi_tx_dev_t *dev, sink_edid_t *sink_cap,
						audioParams_t *pAudio)
{
	/*speakerAllocationDataBlock_t *data =
				&sink_cap->edid_mSpeakerAllocationDataBlock;

	LOG_TRACE();*/
	/*pAudio->mChannelAllocation = get_channell_alloc_code(dev, data);
	pAudio->mChannelAllocation = (pAudio->mChannelAllocation == -1) ?
						0 : pAudio->mChannelAllocation;*/

	/* if there is at least one short audio descriptor */
	/*if (sink_cap->edid_mSadIndex > 0) {
		pAudio->mInterfaceType = I2S;
		pAudio->mCodingType = sink_cap->edid_mSad[0].mFormat;
		pAudio->mPacketType = AUDIO_SAMPLE;

		if (sad_support48k(dev, &sink_cap->edid_mSad[0])) {
			pAudio->mSamplingFrequency = 48000;
		} else if (sad_support44k1(dev, &sink_cap->edid_mSad[0])) {
			pAudio->mSamplingFrequency = 44100;
		} else if (sad_support32k(dev, &sink_cap->edid_mSad[0])) {
			pAudio->mSamplingFrequency = 32000;
		} else {
			HDMI_INFO_MSG("WARNING:Audio Sampling Frequency not supported, configuring for 32k.");
			pAudio->mSamplingFrequency = 32000;
		}
	}*/

	/* choose highest sample size supported by sink */
	/*if (sink_cap->edid_mSad[0].mFormat == 1) {
		if (sad_support24bit(dev, &sink_cap->edid_mSad[0]))
			pAudio->mSampleSize = 24;
		else if (sad_support20bit(dev, &sink_cap->edid_mSad[0]))
			pAudio->mSampleSize = 20;
		else if (sad_support16bit(dev, &sink_cap->edid_mSad[0]))
			pAudio->mSampleSize = 16;
	} else {
		pAudio->mSampleSize = 16;
	}*/

	pAudio->mLevelShiftValue = 0;
	pAudio->mDownMixInhibitFlag = 0;
	pAudio->mLevelShiftValue = 128;

}


static int _edid_struture_parser(hdmi_tx_dev_t *dev, struct edid *edid,
						sink_edid_t *sink)
{
	int i;
	char *monitorName;

	if (edid->header[0] != 0) {
		HDMI_INFO_MSG("Invalid Header\n");
		return -1;
	}

	for (i = 0; i < 4; i++) {
		struct detailed_timing *detailed_timing = &(edid->detailed_timings[i]);

		if (detailed_timing->pixel_clock == 0) {
			struct detailed_non_pixel *npixel = &(detailed_timing->data.other_data);

			switch (npixel->type) {
			case EDID_DETAIL_MONITOR_NAME:
				monitorName = (char *) &(npixel->data.str.str);
				HDMI_INFO_MSG("Monitor name: %s\n", monitorName);
				break;
			case EDID_DETAIL_MONITOR_RANGE:
				break;

			}
		} else { /* Detailed Timing Definition */
			struct detailed_pixel_timing *ptiming = &(detailed_timing->data.pixel_data);

			HDMI_INFO_MSG(" time: %d\n", detailed_timing->pixel_clock * 10000);
			HDMI_INFO_MSG("hactive_hblank_hi: %d\n", ptiming->hactive_hblank_hi);
		}
	}
	return true;
}

static int _edid_cea_extension_parser(hdmi_tx_dev_t *dev, u8 *buffer,
						sink_edid_t *edidExt)
{
	int i = 0;
	int c = 0;
	dtd_t tmpDtd;
	u8 offset = buffer[2];

	if (buffer[1] < 0x03) {
		HDMI_INFO_MSG("Invalid version for CEA Extension block,only rev 3 or higher is supported");
		return -1;
	}

	edidExt->edid_mYcc422Support = bit_field(buffer[3],	4, 1) == 1;
	edidExt->edid_mYcc444Support = bit_field(buffer[3],	5, 1) == 1;
	edidExt->edid_mBasicAudioSupport = bit_field(buffer[3], 6, 1) == 1;
	edidExt->edid_mUnderscanSupport = bit_field(buffer[3], 7, 1) == 1;
	if (offset != 4) {
		for (i = 4; i < offset; i += edid_parser_ParseDataBlock(dev, buffer + i, edidExt))
			;
	}
	/* last is checksum */
	for (i = offset, c = 0; i < (sizeof(buffer) - 1) && c < 6; i += DTD_SIZE, c++) {
		if (dtd_parse(dev, &tmpDtd, buffer + i) == true) {
			if (edidExt->edid_mDtdIndex < (sizeof(edidExt->edid_mDtd) / sizeof(dtd_t))) {
				edidExt->edid_mDtd[edidExt->edid_mDtdIndex++] = tmpDtd;
				HDMI_INFO_MSG("edid_mDtd code %d\n", edidExt->edid_mDtd[edidExt->edid_mDtdIndex].mCode);
				HDMI_INFO_MSG("edid_mDtd limited to Ycc420? %d\n", edidExt->edid_mDtd[edidExt->edid_mDtdIndex].mLimitedToYcc420);
				HDMI_INFO_MSG("edid_mDtd supports Ycc420? %d\n", edidExt->edid_mDtd[edidExt->edid_mDtdIndex].mYcc420);
			} else {
				HDMI_INFO_MSG("buffer full - DTD ignored\n");
			}
		}
	}
	return true;
}

static int edid_parser(hdmi_tx_dev_t *dev, u8 *buffer, sink_edid_t *edidExt,
							u16 edid_size)
{
	int ret = 0;

	switch (buffer[0]) {
	case 0x00:
		ret = _edid_struture_parser(dev, (struct edid *) buffer,
								edidExt);
		break;
	case CEA_EXT:
		ret = _edid_cea_extension_parser(dev, buffer, edidExt);
		break;
	case VTB_EXT:
	case DI_EXT:
	case LS_EXT:
	case MI_EXT:
	default:
		HDMI_INFO_MSG("Block 0x%02x not supported\n", buffer[0]);
	}
	if (ret == true)
		return true;

	return false;
}

static int edid_parser_CeaExtReset(hdmi_tx_dev_t *dev, sink_edid_t *edidExt)
{
	unsigned i = 0;

	edidExt->edid_m20Sink = false;
#if 1
	for (i = 0; i < sizeof(edidExt->edid_mMonitorName); i++)
		edidExt->edid_mMonitorName[i] = 0;

	edidExt->edid_mBasicAudioSupport = false;
	edidExt->edid_mUnderscanSupport = false;
	edidExt->edid_mYcc422Support = false;
	edidExt->edid_mYcc444Support = false;
	edidExt->edid_mYcc420Support = false;
	edidExt->edid_mDtdIndex = 0;
	edidExt->edid_mSadIndex = 0;
	edidExt->edid_mSvdIndex = 0;
#endif
	hdmivsdb_reset(dev, &edidExt->edid_mHdmivsdb);
	hdmiforumvsdb_reset(dev, &edidExt->edid_mHdmiForumvsdb);
	monitor_range_limits_reset(dev, &edidExt->edid_mMonitorRangeLimits);
	video_cap_data_block_reset(dev,
				&edidExt->edid_mVideoCapabilityDataBlock);
	colorimetry_data_block_reset(dev, &edidExt->edid_mColorimetryDataBlock);
	hdr_metadata_data_block_reset(dev, &edidExt->edid_hdr_static_metadata_data_block);
	speaker_alloc_data_block_reset(dev,
				&edidExt->edid_mSpeakerAllocationDataBlock);
	return true;
}

static int edid_parser_ParseDataBlock(hdmi_tx_dev_t *dev, u8 *data,
						sink_edid_t *edidExt)
{
	u8 c = 0;
	shortAudioDesc_t tmpSad;
	shortVideoDesc_t tmpSvd;
	u8 tmpYcc420All = 0;
	u8 tmpLimitedYcc420All = 0;
	u32 ieeeId = 0;
	u8 extendedTag = 0;
	int i = 0;
	int edid_cnt = 0;
	int svdNr = 0;
	int icnt = 0;
	u8 tag = bit_field(data[0], 5, 3);
	u8 length = bit_field(data[0], 0, 5);

	tmpSvd.mLimitedToYcc420 = 0;
	tmpSvd.mYcc420 = 0;


	switch (tag) {
	case 0x1:		/* Audio Data Block */
		HDMI_INFO_MSG("EDID: Audio datablock parsing\n");
		for (c = 1; c < (length + 1); c += 3) {
			sad_parse(dev, &tmpSad, data + c);
			if (edidExt->edid_mSadIndex < (sizeof(edidExt->edid_mSad) / sizeof(shortAudioDesc_t)))
				edidExt->edid_mSad[edidExt->edid_mSadIndex++] = tmpSad;
			else
				HDMI_INFO_MSG("buffer full - SAD ignored\n");
		}
		break;
	case 0x2:		/* Video Data Block */
		HDMI_INFO_MSG("EDID: Video datablock parsing\n");
		for (c = 1; c < (length + 1); c++) {
			svd_parse(dev, &tmpSvd, data[c]);
			if (edidExt->edid_mSvdIndex < (sizeof(edidExt->edid_mSvd) / sizeof(shortVideoDesc_t)))
				edidExt->edid_mSvd[edidExt->edid_mSvdIndex++] = tmpSvd;
			else
				HDMI_INFO_MSG("buffer full - SVD ignored\n");
		}
		break;
	case 0x3:		/* Vendor Specific Data Block HDMI or HF */
		HDMI_INFO_MSG("EDID: VSDB HDMI and HDMI-F\n ");
		ieeeId = byte_to_dword(0x00, data[3], data[2], data[1]);
		if (ieeeId == 0x000C03) {	/* HDMI */
			if (hdmivsdb_parse(dev, &edidExt->edid_mHdmivsdb, data) != true) {
				HDMI_INFO_MSG("HDMI Vendor Specific Data Block corrupt\n");
				break;
			}
			HDMI_INFO_MSG("EDID HDMI VSDB parsed\n");
		} else {
			if (ieeeId == 0xC45DD8) {	/* HDMI-F */
				HDMI_INFO_MSG("Sink is HDMI 2.0 because haves HF-VSDB\n");
				edidExt->edid_m20Sink = true;
				if (hdmiforumvsdb_parse(dev, &edidExt->edid_mHdmiForumvsdb, data) != true) {
					HDMI_INFO_MSG("HDMI Vendor Specific Data Block corrupt\n");
					break;
				} else {
#if 0
					if (edidExt->edid_mHdmiForumvsdb.mLTS_340Mcs_scramble == 1)
						scrambling_Enable(baseAddr, 1);
					else
						scrambling_Enable(baseAddr, 0);
#endif
				}
			} else {
				HDMI_INFO_MSG("Vendor Specific Data Block not parsed ieeeId: 0x%x\n",
						ieeeId);
			}
		}
		break;
	case 0x4:		/* Speaker Allocation Data Block */
		HDMI_INFO_MSG("SAD block parsing");
		if (speaker_alloc_data_block_parse(dev, &edidExt->edid_mSpeakerAllocationDataBlock, data) != true)
			HDMI_INFO_MSG("Speaker Allocation Data Block corrupt\n");
		break;
	case 0x7:{
		HDMI_INFO_MSG("EDID CEA Extended field 0x07\n");
		extendedTag = data[1];
		switch (extendedTag) {
		case 0x00:	/* Video Capability Data Block */
			HDMI_INFO_MSG("Video Capability Data Block\n");
			if (video_cap_data_block_parse(dev, &edidExt->edid_mVideoCapabilityDataBlock, data) != true)
				HDMI_INFO_MSG("Video Capability Data Block corrupt\n");
			break;
		case 0x04:	/* HDMI Video Data Block */
			HDMI_INFO_MSG("HDMI Video Data Block\n");
			break;
		case 0x05:	/* Colorimetry Data Block */
			HDMI_INFO_MSG("Colorimetry Data Block");
			if (colorimetry_data_block_parse(dev, &edidExt->edid_mColorimetryDataBlock, data) != true)
				HDMI_INFO_MSG("Colorimetry Data Block corrupt\n");
			break;
		case 0x06:	/* HDR Static Metadata Data Block */
			HDMI_INFO_MSG("HDR Static Metadata Data Block");
			if (hdr_static_metadata_block_parse(dev, &edidExt->edid_hdr_static_metadata_data_block, data) != true)
				HDMI_INFO_MSG("HDR Static Metadata Data Block\n");
			break;
		case 0x12:	/* HDMI Audio Data Block */
			HDMI_INFO_MSG("HDMI Audio Data Block\n");
			break;
		case 0xe:
			/** If it is a YCC420 VDB then VICs can ONLY be displayed in YCC 4:2:0 */
			HDMI_INFO_MSG("YCBCR 4:2:0 Video Data Block\n");
			/** If Sink has YCC Datablocks it is HDMI 2.0 */
			edidExt->edid_m20Sink = true;
			tmpLimitedYcc420All = (bit_field(data[0], 0, 5) == 1 ? 1 : 0);
			edid_parser_updateYcc420(edidExt, tmpYcc420All, tmpLimitedYcc420All);
			for (i = 0; i < (bit_field(data[0], 0, 5) - 1); i++) {
				/** Length includes the tag byte*/
				tmpSvd.mCode = data[2 + i];
				tmpSvd.mNative = 0;
				tmpSvd.mLimitedToYcc420 = 1;
				for (edid_cnt = 0; edid_cnt < edidExt->edid_mSvdIndex; edid_cnt++) {
					if (edidExt->edid_mSvd[edid_cnt].mCode == tmpSvd.mCode) {
						edidExt->edid_mSvd[edid_cnt] =	tmpSvd;
						goto concluded;
					}
				}
				if (edidExt->edid_mSvdIndex <
					(sizeof(edidExt->edid_mSvd) /  sizeof(shortVideoDesc_t))) {
					edidExt->edid_mSvd[edidExt->edid_mSvdIndex] = tmpSvd;
					edidExt->edid_mSvdIndex++;
				} else {
					HDMI_INFO_MSG("buffer full - YCC 420 DTD ignored");
				}
concluded:;
			}
			break;
		case 0x0f:
			/** If it is a YCC420 CDB then VIC can ALSO be displayed in YCC 4:2:0 */
			edidExt->edid_m20Sink = true;
			HDMI_INFO_MSG("YCBCR 4:2:0 Capability Map Data Block");
			/* If YCC420 CMDB is bigger than 1, then there is SVD info to parse */
			if (bit_field(data[0], 0, 5) > 1) {
				for (icnt = 0; icnt < 8; icnt++) {
					/** Lenght includes the tag byte*/
					if ((bit_field(data[2], 1 >> icnt, 1) & 0x01)) {
						svdNr = icnt;
						tmpSvd.mCode = edidExt->edid_mSvd[svdNr - 1].mCode;
						tmpSvd.mYcc420 = 1;
						edidExt->edid_mSvd[svdNr - 1] = tmpSvd;
					}
				}
				/* Otherwise, all SVDs present at the Video Data Block support YCC420*/
			} else {
				tmpYcc420All = (bit_field(data[0], 0, 5) == 1 ? 1 : 0);
				edid_parser_updateYcc420(edidExt, tmpYcc420All, tmpLimitedYcc420All);
			}
			break;
		default:
			HDMI_INFO_MSG("Extended Data Block not parsed %d\n",
					extendedTag);
			break;
		}
		break;
	}
	default:
		HDMI_INFO_MSG("Data Block not parsed %d\n", tag);
		break;
	}
	return length + 1;
}

static void edid_parser_updateYcc420(sink_edid_t *edidExt,
					u8 Ycc420All, u8 LimitedToYcc420All)
{
	u16 edid_cnt = 0;

	for (edid_cnt = 0; edid_cnt < edidExt->edid_mSvdIndex; edid_cnt++) {
		switch (edidExt->edid_mSvd[edid_cnt].mCode) {
		case 96:
		case 97:
		case 101:
		case 102:
		case 106:
		case 107:
			Ycc420All == 1 ?
				edidExt->edid_mSvd[edid_cnt].mYcc420 = Ycc420All : 0;
			LimitedToYcc420All == 1 ?
				edidExt->edid_mSvd[edid_cnt].mLimitedToYcc420 =
							LimitedToYcc420All : 0;
			break;
		}
	}
}

