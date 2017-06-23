/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef EDID_H_
#define EDID_H_

#include "hdmitx_dev.h"
#include "general_ops.h"
#include "log.h"

#define MAX_HDMI_VIC		16
#define MAX_HDMI_3DSTRUCT	16
#define MAX_VIC_WITH_3D		16

#define EDID_LENGTH 128
#define DDC_ADDR 0x50

#define CEA_EXT	    0x02
#define VTB_EXT	    0x10
#define DI_EXT	    0x40
#define LS_EXT	    0x50
#define MI_EXT	    0x60


/**
 * @file
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */
typedef struct {
	/** VIC code */
	u32 mCode;

	/** Identifies modes that ONLY can be displayed in YCC 4:2:0 */
	u8 mLimitedToYcc420;

	/** Identifies modes that can also be displayed in YCC 4:2:0 */
	u8 mYcc420;

	u16 mPixelRepetitionInput;

	/** In units of 1KHz */
	u32 mPixelClock;

	/** 1 for interlaced, 0 progressive */
	u8 mInterlaced;

	u16 mHActive;

	u16 mHBlanking;

	u16 mHBorder;

	u16 mHImageSize; /*For picture aspect ratio*/

	u16 mHSyncOffset;

	u16 mHSyncPulseWidth;

	/** 0 for Active low, 1 active high */
	u8 mHSyncPolarity;

	u16 mVActive;

	u16 mVBlanking;

	u16 mVBorder;

	u16 mVImageSize; /*For picture aspect ratio*/

	u16 mVSyncOffset;

	u16 mVSyncPulseWidth;

	/** 0 for Active low, 1 active high */
	u8 mVSyncPolarity;

} dtd_t;

typedef enum {
	EDID_ERROR = 0,
	EDID_IDLE,
	EDID_READING,
	EDID_DONE
} edid_status_t;

typedef struct supported_dtd {
	u32 refresh_rate;/*  1HZ * 1000  */
	dtd_t dtd;
} supported_dtd_t;

/**
 * @file
 * Second Monitor Descriptor
 * Parse and hold Monitor Range Limits information read from EDID
 */
typedef struct {
	u8 mMinVerticalRate;

	u8 mMaxVerticalRate;

	u8 mMinHorizontalRate;

	u8 mMaxHorizontalRate;

	u8 mMaxPixelClock;

	int mValid;
} monitorRangeLimits_t;


/**
 * @file
 * Colorimetry Data Block class.
 * Holds and parses the Colorimetry data-block information.
 */

typedef struct {
	u8 mByte3;

	u8 mByte4;

	int mValid;

} colorimetryDataBlock_t;

struct hdr_static_metadata_data_block {
	u8 et_n;
	u8 sm_n;

	/*Desired Content Max Luminance data*/
	u8 dc_max_lum_data;

	/*Desired Content Max Frame-average Luminance data*/
	u8 dc_max_fa_lum_data;

	/*Desired Content Min Luminance data*/
	u8 dc_min_lum_data;
};

/* HDMI 2.0 HF_VSDB */
typedef struct {
	u32 mIeee_Oui;

	u8 mValid;

	u8 mVersion;

	u8 mMaxTmdsCharRate;

	u8 m3D_OSD_Disparity;

	u8 mDualView;

	u8 mIndependentView;

	u8 mLTS_340Mcs_scramble;

	u8 mRR_Capable;

	u8 mSCDC_Present;

	u8 mDC_30bit_420;

	u8 mDC_36bit_420;

	u8 mDC_48bit_420;

} hdmiforumvsdb_t;


/*For detailed handling of this structure,
	refer to documentation of the functions */
typedef struct {
	u16 mPhysicalAddress; /*physical address for cec */

	int mSupportsAi; /*Support ACP ISRC1 ISRC2 packets*/

	int mDeepColor30;

	int mDeepColor36;

	int mDeepColor48;

	int mDeepColorY444;

	int mDviDual; /*Support DVI dual-link operation*/

	u16 mMaxTmdsClk;

	u16 mVideoLatency;

	u16 mAudioLatency;

	u16 mInterlacedVideoLatency;

	u16 mInterlacedAudioLatency;

	u32 mId;
	/*Sink Support for some particular content types*/
	u8 mContentTypeSupport;

	u8 mImageSize; /*for picture espect ratio*/

	int mHdmiVicCount;

	u8 mHdmiVic[MAX_HDMI_VIC];/*the max vic length in vsdb is MAX_HDMI_VIC*/

	int m3dPresent;
	/* row index is the VIC number */
	int mVideo3dStruct[MAX_VIC_WITH_3D][MAX_HDMI_3DSTRUCT];
	/*row index is the VIC number */
	int mDetail3d[MAX_VIC_WITH_3D][MAX_HDMI_3DSTRUCT];

	int mValid;

} hdmivsdb_t;

/**
 * @file
 * Short Audio Descriptor.
 * Found in Audio Data Block (shortAudioDesc_t *sad, CEA Data Block Tage Code 1)
 * Parse and hold information from EDID data structure
 */
/** For detailed handling of this structure, refer to documentation
	of the functions */
typedef struct {
	u8 mFormat;

	u8 mMaxChannels;

	u8 mSampleRates;

	u8 mByte3;
} shortAudioDesc_t;

/**
 * @file
 * Short Video Descriptor.
 * Parse and hold Short Video Descriptors found in Video Data Block in EDID.
 */
/** For detailed handling of this structure,
	refer to documentation of the functions */
typedef struct {
	int	mNative;

	unsigned mCode;

	unsigned mLimitedToYcc420;

	unsigned mYcc420;

} shortVideoDesc_t;

/**
 * @file
 * SpeakerAllocation Data Block.
 * Holds and parse the Speaker Allocation data block information.
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */

typedef struct {
	u8 mByte1;
	int mValid;
} speakerAllocationDataBlock_t;


typedef struct speaker_alloc_code {
	unsigned char byte;
	unsigned char code;
} speaker_alloc_code_t;

/**
 * @file
 * Video Capability Data Block.
 * (videoCapabilityDataBlock_t * vcdbCEA Data Block Tag Code 0).
 * Parse and hold information from EDID data structure.
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */

typedef struct {
	int mQuantizationRangeSelectable;

	u8 mPreferredTimingScanInfo;

	u8 mItScanInfo;

	u8 mCeScanInfo;

	int mValid;
} videoCapabilityDataBlock_t;

struct est_timings {
	u8 t1;
	u8 t2;
	u8 mfg_rsvd;
} __packed;


struct std_timing {
	u8 hsize; /* need to multiply by 8 then add 248 */
	u8 vfreq_aspect;
} __packed;


/* If detailed data is pixel timing */
struct detailed_pixel_timing {
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hactive_hblank_hi;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vactive_vblank_hi;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_offset_pulse_width_lo;
	u8 hsync_vsync_offset_pulse_width_hi;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 width_height_mm_hi;
	u8 hborder;
	u8 vborder;
	u8 misc;
} __packed;

/* If it's not pixel timing, it'll be one of the below */
struct detailed_data_string {
	u8 str[13];
} __packed;

struct detailed_data_monitor_range {
	u8 min_vfreq;
	u8 max_vfreq;
	u8 min_hfreq_khz;
	u8 max_hfreq_khz;
	u8 pixel_clock_mhz; /* need to multiply by 10 */
	u8 flags;
	union {
		struct {
			u8 reserved;
			u8 hfreq_start_khz; /* need to multiply by 2 */
			u8 c; /* need to divide by 2 */
			u16 m;
			u8 k;
			u8 j; /* need to divide by 2 */
		} __packed gtf2;
		struct {
			u8 version;
			u8 data1; /* high 6 bits: extra clock resolution */
			u8 data2; /* plus low 2 of above: max hactive */
			u8 supported_aspects;
			u8 flags; /* preferred aspect and blanking support */
			u8 supported_scalings;
			u8 preferred_refresh;
		} __packed cvt;
	} formula;
} __packed;

struct detailed_data_wpindex {
	u8 white_yx_lo; /* Lower 2 bits each */
	u8 white_x_hi;
	u8 white_y_hi;
	u8 gamma; /* need to divide by 100 then add 1 */
} __packed;



struct cvt_timing {
	u8 code[3];
} __packed;

struct detailed_non_pixel {
	u8 pad1;
	u8 type; /* ff=serial, fe=string, fd=monitor range, fc=monitor name
		    fb=color point data, fa=standard timing data,
		    f9=undefined, f8=mfg. reserved */
	u8 pad2;
	union {
		struct detailed_data_string str;
		struct detailed_data_monitor_range range;
		struct detailed_data_wpindex color;
		struct std_timing timings[6];
		struct cvt_timing cvt[4];
	} data;
} __packed;

#define EDID_DETAIL_EST_TIMINGS 0xf7
#define EDID_DETAIL_CVT_3BYTE 0xf8
#define EDID_DETAIL_COLOR_MGMT_DATA 0xf9
#define EDID_DETAIL_STD_MODES 0xfa
#define EDID_DETAIL_MONITOR_CPDATA 0xfb
#define EDID_DETAIL_MONITOR_NAME 0xfc
#define EDID_DETAIL_MONITOR_RANGE 0xfd
#define EDID_DETAIL_MONITOR_STRING 0xfe
#define EDID_DETAIL_MONITOR_SERIAL 0xff

struct detailed_timing {
	u16 pixel_clock; /* need to multiply by 10 KHz */
	union {
		struct detailed_pixel_timing pixel_data;
		struct detailed_non_pixel other_data;
	} data;
} __packed;


struct edid {
	u8 header[8];
	/* Vendor & product info */
	u8 mfg_id[2];
	u8 prod_code[2];
	u32 serial; /* FIXME: byte order */
	u8 mfg_week;
	u8 mfg_year;
	/* EDID version */
	u8 version;
	u8 revision;
	/* Display info: */
	u8 input;
	u8 width_cm;
	u8 height_cm;
	u8 gamma;
	u8 features;
	/* Color characteristics */
	u8 red_green_lo;
	u8 black_white_lo;
	u8 red_x;
	u8 red_y;
	u8 green_x;
	u8 green_y;
	u8 blue_x;
	u8 blue_y;
	u8 white_x;
	u8 white_y;
	/* Est. timings and mfg rsvd timings*/
	struct est_timings established_timings;
	/* Standard timings 1-8*/
	struct std_timing standard_timings[8];
	/* Detailing timings 1-4 */
	struct detailed_timing detailed_timings[4];
	/* Number of 128 byte ext. blocks */
	u8 extensions;
	/* Checksum */
	u8 checksum;
} __packed;


/* Short Audio Descriptor */
struct cea_sad {
	u8 format;
	u8 channels; /* max number of channels - 1 */
	u8 freq;
	u8 byte2; /* meaning depends on format */
};

extern u16 byte_to_word(const u8 hi, const u8 lo);
extern u8 bit_field(const u16 data, u8 shift, u8 width);
extern u16 concat_bits(u8 bHi, u8 oHi, u8 nHi, u8 bLo, u8 oLo, u8 nLo);


/**
 * Parses the Detailed Timing Descriptor.
 * Encapsulating the parsing process
 * @param dtd pointer to dtd_t strucutute for the information to be save in
 * @param data a pointer to the 18-byte structure to be parsed.
 * @return TRUE if success
 */
int dtd_parse(hdmi_tx_dev_t *dev, dtd_t *dtd, u8 data[18]);

/**
 * @param dtd pointer to dtd_t strucutute for the information to be save in
 * @param code the CEA 861-D video code.
 * @param refreshRate the specified vertical refresh rate.
 * @return TRUE if success
 */
int dtd_fill(hdmi_tx_dev_t *dev, dtd_t *dtd, u8 code, u32 refreshRate);

/**
 * @param dtd Pointer to the current DTD parameters
 * @return The refresh rate if DTD parameters are correct and 0 if not
 */
u32 dtd_get_refresh_rate(dtd_t *dtd);

/**
 * @param dtd Pointer to the current DTD parameters
 * @param tempDtd Pointer to the temp DTD parameters
 * @return The refresh rate if DTD parameters are correct and 0 if not
 */
void dtd_change_horiz_for_ycc420(hdmi_tx_dev_t *dev, dtd_t *tempDtd);

void monitor_range_limits_reset(hdmi_tx_dev_t *dev, monitorRangeLimits_t *mrl);

void colorimetry_data_block_reset(hdmi_tx_dev_t *dev,
					colorimetryDataBlock_t *cdb);
void hdr_metadata_data_block_reset(hdmi_tx_dev_t *dev,
		struct hdr_static_metadata_data_block *hdr_metadata);

int colorimetry_data_block_parse(hdmi_tx_dev_t *dev,
					colorimetryDataBlock_t *cdb, u8 *data);

int hdr_static_metadata_block_parse(hdmi_tx_dev_t *dev,
		struct hdr_static_metadata_data_block *hdr_metadata, u8 *data);

void hdmiforumvsdb_reset(hdmi_tx_dev_t *dev, hdmiforumvsdb_t *forumvsdb);
int hdmiforumvsdb_parse(hdmi_tx_dev_t *dev,
					hdmiforumvsdb_t *forumvsdb, u8 *data);

void hdmivsdb_reset(hdmi_tx_dev_t *dev, hdmivsdb_t *vsdb);

/**
 * Parse an array of data to fill the hdmivsdb_t data strucutre
 * @param *vsdb pointer to the structure to be filled
 * @param *data pointer to the 8-bit data type array to be parsed
 * @return Success, or error code:
 * @return 1 - array pointer invalid
 * @return 2 - Invalid datablock tag
 * @return 3 - Invalid minimum length
 * @return 4 - HDMI IEEE registration identifier not valid
 * @return 5 - Invalid length - latencies are not valid
 * @return 6 - Invalid length - Interlaced latencies are not valid
 */
int hdmivsdb_parse(hdmi_tx_dev_t *dev, hdmivsdb_t *vsdb, u8 *data);


void sad_reset(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);

/**
 * Parse Short Audio Descriptor
 */
int sad_parse(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad, u8 *data);

/**
 *@return the sample rates byte, where bit 7 is always 0
 *and rates are sorted respectively starting with bit 6:
 * 192 kHz  176.4 kHz  96 kHz  88.2 kHz  48 kHz  44.1 kHz  32 kHz
 */
/* u8 sad_GetSampleRates(hdmi_tx_dev_t *dev, shortAudioDesc_t * sad); */

int sad_support32k(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support44k1(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support48k(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support88k2(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support96k(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support176k4(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support192k(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support16bit(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support20bit(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int sad_support24bit(hdmi_tx_dev_t *dev, shortAudioDesc_t *sad);
int svd_parse(hdmi_tx_dev_t *dev, shortVideoDesc_t *svd, u8 data);


/* speaker_alloc_data_block_reset */
void speaker_alloc_data_block_reset(hdmi_tx_dev_t *dev,
					speakerAllocationDataBlock_t *sadb);
int speaker_alloc_data_block_parse(hdmi_tx_dev_t *dev,
				speakerAllocationDataBlock_t *sadb, u8 *data);

/**
 * @return the Channel Allocation code used
 *in the Audio Info frame to ease the translation process
 */
u8 get_channell_alloc_code(hdmi_tx_dev_t *dev,
			speakerAllocationDataBlock_t *sadb);

void video_cap_data_block_reset(hdmi_tx_dev_t *dev,
				videoCapabilityDataBlock_t *vcdb);
int video_cap_data_block_parse(hdmi_tx_dev_t *dev,
			videoCapabilityDataBlock_t *vcdb, u8 *data);


int edid_extension_read(hdmi_tx_dev_t *dev, int block, u8 *edid_ext);
int edid_read(hdmi_tx_dev_t *dev, struct edid *edid);


#endif	/* EDID_H_ */
