/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include "hdmi_core.h"
#include "api/api.h"
#include "api/scdc.h"

static u32 hdmi_decode;
static u32 hdcp_irq;

static uintptr_t hdmi_reg_base;
static struct hdmi_tx_core *p_core;

static u32 audio_start_config;

struct disp_video_timings info;
static struct disp_hdmi_mode hdmi_mode_tbl[] = {
	{DISP_TV_MOD_480I,                HDMI1440_480I,     },
	{DISP_TV_MOD_576I,                HDMI1440_576I,     },
	{DISP_TV_MOD_480P,                HDMI480P,          },
	{DISP_TV_MOD_576P,                HDMI576P,          },
	{DISP_TV_MOD_720P_50HZ,           HDMI720P_50,       },
	{DISP_TV_MOD_720P_60HZ,           HDMI720P_60,       },
	{DISP_TV_MOD_1080I_50HZ,          HDMI1080I_50,      },
	{DISP_TV_MOD_1080I_60HZ,          HDMI1080I_60,      },
	{DISP_TV_MOD_1080P_24HZ,          HDMI1080P_24,      },
	{DISP_TV_MOD_1080P_50HZ,          HDMI1080P_50,      },
	{DISP_TV_MOD_1080P_60HZ,          HDMI1080P_60,      },
	{DISP_TV_MOD_1080P_25HZ,          HDMI1080P_25,      },
	{DISP_TV_MOD_1080P_30HZ,          HDMI1080P_30,      },
	{DISP_TV_MOD_1080P_24HZ_3D_FP,    HDMI1080P_24_3D_FP,},
	{DISP_TV_MOD_720P_50HZ_3D_FP,     HDMI720P_50_3D_FP, },
	{DISP_TV_MOD_720P_60HZ_3D_FP,     HDMI720P_60_3D_FP, },
	{DISP_TV_MOD_3840_2160P_30HZ,     HDMI3840_2160P_30, },
	{DISP_TV_MOD_3840_2160P_25HZ,     HDMI3840_2160P_25, },
	{DISP_TV_MOD_3840_2160P_24HZ,     HDMI3840_2160P_24, },
	{DISP_TV_MOD_4096_2160P_24HZ,     HDMI4096_2160P_24, },
	{DISP_TV_MOD_4096_2160P_25HZ,	  HDMI4096_2160P_25, },
	{DISP_TV_MOD_4096_2160P_30HZ,	  HDMI4096_2160P_30, },
	{DISP_TV_MOD_3840_2160P_60HZ,     HDMI3840_2160P_60, },
	{DISP_TV_MOD_4096_2160P_60HZ,     HDMI4096_2160P_60, },
};

static int hdmitx_set_phy(struct hdmi_tx_core *core, int phy);
static void core_init_hdcp(struct hdmi_mode *cfg);
static struct hdmi_mode *core_get_user_config(struct hdmi_tx_core *core);
static void core_init_video(struct hdmi_mode *cfg);
static void core_init_audio(struct hdmi_mode *cfg);

static void update_dtd_cfg(dtd_t *user, dtd_t *cfg);
static u32 svd_user_config(u32 code, u32 refresh_rate);
static void print_videoinfo(videoParams_t *pVideo);
static void print_audioinfo(audioParams_t *audio);

static void update_video_cfg(videoParams_t *user, videoParams_t *cfg);

static void hdmitx_sleep(int us)
{
	udelay(us);
}

void hdmi_core_set_base_addr(uintptr_t reg_base)
{
	hdmi_reg_base = reg_base;
}

uintptr_t hdmi_core_get_base_addr(void)
{
	return hdmi_reg_base;
}

/*******************************
 * HDMI TX Support functions   *
 ******************************/
void set_platform(struct hdmi_tx_core *core)
{
	p_core = core;
}

struct hdmi_tx_core *get_platform(void)
{
	return p_core;
}

static void hdmitx_write(uintptr_t addr, u32 data)
{
	asm volatile("dsb st");
	*((volatile u8 *)(hdmi_core_get_base_addr() + (addr >> 2))) = data;

}

static u32 hdmitx_read(uintptr_t addr)
{
	return *((volatile u8 *)(hdmi_core_get_base_addr() + (addr >> 2)));
}

void resistor_calibration_core(struct hdmi_tx_core *core, u32 reg, u32 data)
{
	 hdmitx_sleep(1000);
	resistor_calibration(&core->hdmi_tx, reg, data);
}

u32 hdmi_core_get_hpd_state(void)
{
	struct hdmi_tx_core *core = get_platform();
	return phy_hot_plug_state(&core->hdmi_tx);
}

static void core_init_hdcp(struct hdmi_mode *cfg)
{
	hdcpParams_t *pHdcp = &(cfg->pHdcp);

	pHdcp->bypass = -1;
	pHdcp->mEnable11Feature = -1;
	pHdcp->mRiCheck = -1;
	pHdcp->mI2cFastMode = -1;
	pHdcp->mEnhancedLinkVerification = -1;
	pHdcp->maxDevices = 0;
	pHdcp->mKsvListBuffer = NULL;
	pHdcp->mAksv = NULL;
	pHdcp->mKeys = NULL;
	pHdcp->mSwEncKey = NULL;
}

static void core_init_video(struct hdmi_mode *cfg)
{
	videoParams_t *video = &(cfg->pVideo);

	memset(video, 0, sizeof(videoParams_t));

	video->mHdmi = MODE_UNDEFINED;
	video->mEncodingOut = ENC_UNDEFINED;
	video->mEncodingIn = ENC_UNDEFINED;
	video->mColorResolution = COLOR_DEPTH_INVALID;
	video->mColorimetry = (u8)(~0);
	video->mEndTopBar = (u16)(~0);
	video->mStartBottomBar = (u16)(~0);
	video->mStartRightBar = (u16)(~0);

	video->mDtd.mLimitedToYcc420 = 0xFF;
	video->mDtd.mYcc420 = 0xFF;
	video->mDtd.mInterlaced = 0xFF;
	video->mDtd.mHBorder = 0xFFFF;
	video->mDtd.mHSyncPolarity = 0xFF;
	video->mDtd.mVBorder = 0xFFFF;
	video->mDtd.mVSyncPolarity = 0xFF;
}

static void core_init_audio(struct hdmi_mode *cfg)
{
	audioParams_t *audio = &(cfg->pAudio);

	memset(audio, 0, sizeof(audioParams_t));

	audio->mInterfaceType = I2S;
	audio->mCodingType = PCM;
	audio->mSamplingFrequency = 44100;
	audio->mChannelAllocation = 0;
	audio->mChannelNum = 2;
	audio->mSampleSize = 16;
	audio->mClockFsFactor = 64;
	audio->mPacketType = PACKET_NOT_DEFINED;
	audio->mDmaBeatIncrement = DMA_NOT_DEFINED;

	audio_start_config = 1;
}

static struct hdmi_mode *core_get_user_config(struct hdmi_tx_core *core)
{
	if (core->user_cfg == NULL) {
		core->user_cfg = kmalloc(sizeof(struct hdmi_mode), GFP_KERNEL);
		if (core->user_cfg == NULL) {
			HDMI_INFO_MSG("Memory allocation for user configuration failed: Exiting\n");
			return core->user_cfg;
		}
		core_init_hdcp(core->user_cfg);
		core_init_video(core->user_cfg);
		core_init_audio(core->user_cfg);
	}

	return core->user_cfg;
}

static int _api_init(struct hdmi_tx_core *core)
{
	hdmitx_api_init(&(core->hdmi_tx), "HDMI Tx App");

	/* Register functions into api layer */
	register_system_functions(&(core->sys_functions));
	register_bsp_functions(&(core->dev_access));

	core->api_func.phy_write = phy_i2c_write;
	core->api_func.phy_read = phy_i2c_read;
	core->api_func.scdc_write = scdc_write;
	core->api_func.scdc_read = scdc_read;
	return 0;
}

int hdmi_tx_core_init(struct hdmi_tx_core *core, int phy)
{
	/* Zero the core and device structrue */
	memset(core, 0, sizeof(struct hdmi_tx_core));
	core->hdmi_tx_phy = phy;

	core->hpd_state = HDMI_State_Idle;

	core->hdmi_tx.snps_hdmi_ctrl.hdmi_on = 1;

	/*user_cfg is used to configure the params of video,audio,hdcp*/
	core->user_cfg = kzalloc(sizeof(struct hdmi_mode), GFP_KERNEL);
	if (core->user_cfg == NULL) {
		HDMI_INFO_MSG("Memory allocation for user configuration failed: Exiting\n");
		return -1;
	}
	memset(core->user_cfg, 0, sizeof(struct hdmi_mode));

	core_init_hdcp(core->user_cfg);
	core_init_video(core->user_cfg);
	core_init_audio(core->user_cfg);

	/* Set global variable */
	set_platform(core);

	/* set device access functions */
	strcpy(core->dev_access.name, "HDMI TX Device Access");
	core->dev_access.read = hdmitx_read;
	core->dev_access.write = hdmitx_write;

	/* set system abstraction functions */
	strcpy(core->sys_functions.name, "HDMI TX System Functions");
	core->sys_functions.sleep = hdmitx_sleep;

	if (hdmitx_set_phy(core, phy)) {
		HDMI_INFO_MSG("Invalid PHY model %d\n", phy);
		return -1;
	}
	HDMI_INFO_MSG("Configuring for PHY E%d\n", phy);

	_api_init(core);

	core->hdmi_tx.snps_hdmi_ctrl.hdcp_on = false; /*enable HDCP1.4*/

	if (!core->hdmi_tx.snps_hdmi_ctrl.hdcp_on)
		core->mode.pHdcp.bypass = true;
	else
		core->mode.pHdcp.bypass = false;

	core->hdmi_tx.snps_hdmi_ctrl.phy_access = PHY_I2C;
	return 0;
}


/**
 * @short Set PHY number
 * @param[in] core Main structure
 * @param[in] phy PHY number:301 or 309
 * @return 0 if successful or -1 if failure
 */
static  int hdmitx_set_phy(struct hdmi_tx_core *core, int phy)
{
	if ((phy > 500) || (phy < 100)) {
		HDMI_INFO_MSG("PHY value outside of range: %d\n", phy);
		return -1;
	}

	core->hdmi_tx_phy = phy;
	return 0;
}

void hdmi_core_exit(struct hdmi_tx_core *core)
{
	kfree(core->user_cfg);
	kfree(core->mode.edid);
	kfree(core->mode.edid_ext);
	kfree(core->mode.sink_cap);
	kfree(core);
}


/********************************************************/
/********************IRQ handler***************************/
/********************************************************/
void hdcp_handler_core(void)
{
	struct hdmi_tx_core *p_core = get_platform();
	int temp = 0;

	LOG_TRACE();
	hdcp_event_handler(&(p_core->hdmi_tx), &temp, hdcp_irq);
	p_core->hpd_state = HDMI_State_HPD_Done;
}

/* extern struct hdmi_tx_drv *get_hdmi_drv(void); */
u32 hdmi_core_get_irq_type(void)
{
	u8 phy_decode = 0;
	u32 irq_type = 0;
	struct hdmi_tx_core *p_core = get_platform();

	LOG_TRACE();

	hdmi_decode = read_interrupt_decode(&p_core->hdmi_tx);
	hdcp_irq = hdcp_interrupt_status(&p_core->hdmi_tx);
	HDMI_INFO_MSG("decode=0x%x, hdcp_irq=0x%x\n", hdmi_decode, hdcp_irq);

	if (hdcp_irq != 0) {
		hdcp_interrupt_clear(&p_core->hdmi_tx, hdcp_irq);
		/* queue_work(get_hdmi_drv()->hdmi_workqueue,
				&get_hdmi_drv()->hdcp_work); */
		irq_type = HDCP_IRQ;
		p_core->hpd_state = HDMI_State_Hdcp_Irq;
	}

	if (decode_is_fc_stat0(hdmi_decode))
		irq_clear_source(&p_core->hdmi_tx, AUDIO_PACKETS);

	if (decode_is_fc_stat1(hdmi_decode))
		irq_clear_source(&p_core->hdmi_tx, OTHER_PACKETS);

	if (decode_is_fc_stat2_vp(hdmi_decode)) {
		/* TODO: mask this for now... */
		irq_mute_source(&p_core->hdmi_tx, PACKETS_OVERFLOW);
		irq_mute_source(&p_core->hdmi_tx, VIDEO_PACKETIZER);
	}

	if (decode_is_as_stat0(hdmi_decode))
		irq_clear_source(&p_core->hdmi_tx, AUDIO_SAMPLER);

	if (decode_is_phy(hdmi_decode)) {
		HDMI_INFO_MSG("PHY interrupt 0x%x\n", hdmi_decode);

		irq_read_stat(&p_core->hdmi_tx, PHY, &phy_decode);
		if (decode_is_phy_lock(phy_decode)) {
			irq_clear_bit(&p_core->hdmi_tx,
				PHY, IH_PHY_STAT0_TX_PHY_LOCK_MASK);
		}

		if (decode_is_phy_hpd(phy_decode)) {
			HDMI_INFO_MSG("HPD received\n");
			phy_hot_plug_detected(&p_core->hdmi_tx);
			irq_clear_bit(&p_core->hdmi_tx, PHY,
						IH_PHY_STAT0_HPD_MASK);
			irq_type = HPD_IRQ;
		}
	}

	if (decode_is_i2c_stat0(hdmi_decode)) {
		uint8_t state = 0;

		irq_read_stat(&p_core->hdmi_tx, I2C_DDC, &state);

		/* I2Cmastererror - I2Cmasterdone */
		if (state & 0x3) {
			irq_clear_bit(&p_core->hdmi_tx, I2C_DDC,
					IH_I2CM_STAT0_I2CMASTERDONE_MASK);
			/* The I2C communication interrupts must be masked
			- they will be handled by polling in the eDDC block */
			HDMI_INFO_MSG("I2C DDC interrupt received 0x%x - mask interrupt\n", state);
		}
		/* SCDC_READREQ */
		else if (state & 0x4) {
			irq_clear_bit(&p_core->hdmi_tx, I2C_DDC,
					IH_I2CM_STAT0_SCDC_READREQ_MASK);
		}
	}

	if (decode_is_cec_stat0(hdmi_decode))
		irq_clear_source(&p_core->hdmi_tx, CEC);


	irq_unmute(&p_core->hdmi_tx);

	return irq_type;
}


void hpd_handler_core(void)
{
	sink_edid_t *sink = NULL;
	/* u32 ret = 0; */
	struct hdmi_tx_core *p_core = get_platform();
	struct hdmi_tx_ctrl *ctrl = &p_core->hdmi_tx.snps_hdmi_ctrl;
	videoParams_t *pVideo = &p_core->mode.pVideo;

	HDMI_INFO_MSG("hdmi_hpd_mask=%x  p_core->hpd_state=%d\n",
				hdmi_hpd_mask, p_core->hpd_state);

	if ((phy_hot_plug_state(&p_core->hdmi_tx) &&
			(p_core->hpd_state == HDMI_State_Wait_Hpd))
		|| ((hdmi_hpd_mask == 0x11) &&
			(p_core->hpd_state == HDMI_State_Wait_Hpd))) {
		pr_info("\n[hdmi2.0]Cable connected\n");

		p_core->hpd_state = HDMI_State_EDID_Parse;
		/* Read sink's EDID */
		edid_read_cap(); /* sink is updated in this function */
		sink = p_core->mode.sink_cap;

		pVideo->mHdmi = HDMI;

		/* Set video and audio mode */
		if (p_core->mode.edid_done != 1) {
			HDMI_INFO_MSG("EDID Read Failed! Set to the mode that disp set, and set defult parameters\n");
		} else {
		/*set defult video and audio params according to sink_cap*/
			edid_set_video_prefered(&p_core->hdmi_tx,
				p_core->mode.sink_cap, &p_core->mode.pVideo);
			edid_set_audio_prefered(&p_core->hdmi_tx,
				p_core->mode.sink_cap, &p_core->mode.pAudio);
		}


		if ((sink != NULL) && (sink->edid_mHdmivsdb.mSupportsAi == 0)) {
			packets_StopSendIsrc1(&p_core->hdmi_tx);
			packets_StopSendIsrc2(&p_core->hdmi_tx);
		}

		ctrl->data_enable_polarity = 1;
		ctrl->phy_access = 1;
		print_videoinfo(&(p_core->mode.pVideo));
		print_audioinfo(&(p_core->mode.pAudio));
		api_Configure(&p_core->hdmi_tx, &p_core->mode.pVideo,
				&p_core->mode.pAudio, &p_core->mode.pProduct,
				&p_core->mode.pHdcp, p_core->hdmi_tx_phy);
		irq_unmute_source(&p_core->hdmi_tx, PHY);
		p_core->hpd_state = HDMI_State_HPD_Done;

	} else if (((!phy_hot_plug_state(&p_core->hdmi_tx))
				&& (p_core->hpd_state == HDMI_State_HPD_Done))
			|| ((hdmi_hpd_mask == 0x10)
				&& (p_core->hpd_state == HDMI_State_HPD_Done))) {
		pr_info("[HDMI2.0]Cable disconnected\n");
		ctrl->hpd = 0;
		ctrl->data_enable_polarity = 0;
		ctrl->phy_access = 0;
		p_core->hpd_state = HDMI_State_Idle;

		/*clear storing edid*/
		if (p_core->mode.sink_cap != NULL)
			kfree(p_core->mode.sink_cap);
		p_core->mode.sink_cap = NULL;

		if (p_core->mode.edid != NULL)
			kfree(p_core->mode.edid);
		p_core->mode.edid = NULL;

		if (p_core->mode.edid_ext != NULL)
			kfree(p_core->mode.edid_ext);
		p_core->mode.edid_ext = NULL;

		hdcp_rxdetect(&p_core->hdmi_tx, 0);
		hdcp_sw_reset(&p_core->hdmi_tx);

		/* Stand by */
		api_Standby(&p_core->hdmi_tx);

		dev_write(&p_core->hdmi_tx, FC_AVICONF0, 0x0);

		p_core->mode.edid_done = 0;
		p_core->hpd_state = HDMI_State_Wait_Hpd;
	}

}


/********************************************************/
/**********************video******************************/
/********************************************************/
static void update_dtd_cfg(dtd_t *user, dtd_t *cfg)
{
	if (user->mCode != 0)
		cfg->mCode = user->mCode;

	if (user->mLimitedToYcc420 != 0xFF)
		cfg->mLimitedToYcc420 = user->mLimitedToYcc420;

	if (user->mYcc420 != 0xFF)
		cfg->mYcc420 = user->mYcc420;

	if (user->mPixelRepetitionInput != 0xFF)
		cfg->mPixelRepetitionInput = user->mPixelRepetitionInput;

	if (user->mPixelClock)
		cfg->mPixelClock = user->mPixelClock;

	if (user->mInterlaced != 0xFF)
		cfg->mInterlaced = user->mInterlaced;

	if (user->mHActive != 0)
		cfg->mHActive = user->mHActive;

	if (user->mHBlanking != 0)
		cfg->mHBlanking = user->mHBlanking;

	if (user->mHBorder != 0xFFFF)
		cfg->mHBorder = user->mHBorder;

	if (user->mHImageSize != 0)
		cfg->mHImageSize = user->mHImageSize;

	if (user->mHSyncOffset != 0)
		cfg->mHSyncOffset = user->mHSyncOffset;

	if (user->mHSyncPulseWidth != 0)
		cfg->mHSyncPulseWidth = user->mHSyncPulseWidth;

	if (user->mHSyncPolarity != 0xFF)
		cfg->mHSyncPolarity = user->mHSyncPolarity;

	if (user->mVActive != 0)
		cfg->mVActive = user->mVActive;

	if (user->mVBlanking != 0)
		cfg->mVBlanking = user->mVBlanking;

	if (user->mVBorder != 0xFFFF)
		cfg->mVBorder = user->mVBorder;

	if (user->mVImageSize != 0)
		cfg->mVImageSize = user->mVImageSize;

	if (user->mVSyncOffset != 0)
		cfg->mVSyncOffset = user->mVSyncOffset;

	if (user->mVSyncPulseWidth != 0)
		cfg->mVSyncPulseWidth = user->mVSyncPulseWidth;

	if (user->mVSyncPolarity != 0xFF)
		cfg->mVSyncPolarity = user->mVSyncPolarity;
}

/*
*@code: svd <1-107, 0x80+4, 0x80+19, 0x80+32>
*@refresh_rate: [optional] Hz*1000,which is 1000 times than 1 Hz
*/
static u32 svd_user_config(u32 code, u32 refresh_rate)
{
	struct hdmi_tx_core *core = get_platform();
	struct hdmi_mode *user_cfg = NULL;
	u32 i;
	dtd_t dtd;
	enum disp_tv_mode tv_mode;

	if (core == NULL) {
		HDMI_INFO_MSG("Empty core structure\n");
		return -1;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_INFO_MSG("ERROR:Improper user_cfg pointer value\n");
		return -1;
	} else {
		memset(&user_cfg->pVideo, 0, sizeof(videoParams_t));
	}

	if (code < 1 || code > 256) {
		HDMI_INFO_MSG("ERROR:VIC Code is out of range\n");
		return -1;
	}

	if (refresh_rate == 0) {
		dtd.mCode = code;
		/* Ensure Pixel clock is 0 to get the default refresh rate */
		dtd.mPixelClock = 0;
		HDMI_INFO_MSG("HDMI_WARN:Code %d with default refresh rate %d\n",
					code, dtd_get_refresh_rate(&dtd)/1000);
	} else {
		HDMI_INFO_MSG("HDMI_WARN:Code %d with refresh rate %d\n",
				code, dtd_get_refresh_rate(&dtd)/1000);
	}

	if (dtd_fill(&core->hdmi_tx, &dtd, code, refresh_rate) == false) {
		HDMI_INFO_MSG("Can not find detailed timing\n");
		return -1;
	}

	for (i = 0; i < sizeof(hdmi_mode_tbl)/sizeof(struct disp_hdmi_mode); i++) {
			if (hdmi_mode_tbl[i].hdmi_mode == (int)code) {
					tv_mode = hdmi_mode_tbl[i].mode;
					break;
			}
	}

	memcpy(&user_cfg->pVideo.mDtd, &dtd, sizeof(dtd_t));
	user_cfg->pVideo.mHdmi = HDMI;

	if (core->mode.sink_cap == NULL) {
	} else {
		edid_set_video_prefered(&core->hdmi_tx, core->mode.sink_cap,
						&core->user_cfg->pVideo);
		if (core->mode.sink_cap->edid_mHdmivsdb.mSupportsAi == 0) {
			packets_StopSendIsrc1(&core->hdmi_tx);
			packets_StopSendIsrc2(&core->hdmi_tx);
		}
	}

	if ((code == 0x80 + 19) || (code == 0x80 + 4) || (code == 0x80 + 32)) {
		user_cfg->pVideo.mHdmiVideoFormat = 0x02;
		user_cfg->pVideo.m3dStructure = 0;
	}

	update_video_cfg(&(user_cfg->pVideo), &(core->mode.pVideo));
	core->hdmi_tx.snps_hdmi_ctrl.data_enable_polarity = 1;
	return 0;
}

static void print_videoinfo(videoParams_t *pVideo)
{
	u32 refresh_rate = dtd_get_refresh_rate(&pVideo->mDtd);

	pr_info("[HDMI2.0]CEA VIC=%d: ", pVideo->mDtd.mCode);
	if (pVideo->mDtd.mInterlaced)
		pr_info("%dx%di", pVideo->mDtd.mHActive,
						pVideo->mDtd.mVActive*2);
	else
		pr_info("%dx%dp", pVideo->mDtd.mHActive,
						pVideo->mDtd.mVActive);
	pr_info("@%d Hz ", refresh_rate/1000);
	pr_info("%d:%d, ", pVideo->mDtd.mHImageSize,
						pVideo->mDtd.mVImageSize);
	pr_info("%d-bpp ", pVideo->mColorResolution);
	pr_info("%s\n", getEncodingString(pVideo->mEncodingIn));
	switch (pVideo->mColorimetry) {
	case 0:
		pr_info("WARN:you haven't set an colorimetry\n");
		break;
	case ITU601:
		pr_info("BT601\n");
		break;
	case ITU709:
		pr_info("BT709\n");
		break;
	case EXTENDED_COLORIMETRY:
		if (pVideo->mExtColorimetry == BT2020_Y_CB_CR)
			pr_info("BT2020_Y_CB_CR\n");
		else
			pr_info("WARN:extended color space standard %d undefined\n", pVideo->mExtColorimetry);
		break;
	default:
		pr_info("WARN:color space standard %d undefined\n", pVideo->mColorimetry);
	}
	if (pVideo->pb == NULL)
		return;

	switch (pVideo->pb->eotf) {
	case SDR_LUMINANCE_RANGE:
		pr_info("eotf:SDR_LUMINANCE_RANGE\n");
		break;
	case HDR_LUMINANCE_RANGE:
		pr_info("eotf:HDR_LUMINANCE_RANGE\n");
		break;
	case SMPTE_ST_2084:
		pr_info("eotf:SMPTE_ST_2084\n");
		break;
	case HLG:
		pr_info("eotf:HLG\n");
		break;
	default:
		pr_info("Unknow eotf\n");
		break;
	}
}


static void print_audioinfo(audioParams_t *audio)
{
	pr_info("Audio interface type = %s\n",
			audio->mInterfaceType == I2S ? "I2S" :
			audio->mInterfaceType == SPDIF ? "SPDIF" :
			audio->mInterfaceType == HBR ? "HBR" :
			audio->mInterfaceType == GPA ? "GPA" :
			audio->mInterfaceType == DMA ? "DMA" : "---");

	pr_info("Audio coding = %s\n", audio->mCodingType == PCM ? "PCM" :
		audio->mCodingType == AC3 ? "AC3" :
		audio->mCodingType == MPEG1 ? "MPEG1" :
		audio->mCodingType == MP3 ? "MP3" :
		audio->mCodingType == MPEG2 ? "MPEG2" :
		audio->mCodingType == AAC ? "AAC" :
		audio->mCodingType == DTS ? "DTS" :
		audio->mCodingType == ATRAC ? "ATRAC" :
		audio->mCodingType == ONE_BIT_AUDIO ? "ONE BIT AUDIO" :
		audio->mCodingType == DOLBY_DIGITAL_PLUS ? "DOLBY DIGITAL +" :
		audio->mCodingType == DTS_HD ? "DTS HD" : "----");
	pr_info("Audio frequency = %dHz\n", audio->mSamplingFrequency);
	pr_info("Audio sample size = %d\n", audio->mSampleSize);
	pr_info("Audio FS factor = %d\n", audio->mClockFsFactor);
	pr_info("Audio ChannelAllocationr = %d\n", audio->mChannelAllocation);
	pr_info("Audio mChannelNum = %d\n", audio->mChannelNum);
}
static void update_video_cfg(videoParams_t *user, videoParams_t *cfg)
{
	if (user->mHdmi != MODE_UNDEFINED)
		cfg->mHdmi = user->mHdmi;

	if (user->mEncodingOut != ENC_UNDEFINED)
		cfg->mEncodingOut = user->mEncodingOut;

	if (user->mEncodingIn != ENC_UNDEFINED)
		cfg->mEncodingIn = user->mEncodingIn;

	if (user->mColorResolution != COLOR_DEPTH_INVALID)
		cfg->mColorResolution = user->mColorResolution;

	if (user->mPixelRepetitionFactor != 0)
		cfg->mPixelRepetitionFactor = user->mPixelRepetitionFactor;

	if (user->mRgbQuantizationRange != 0)
		cfg->mRgbQuantizationRange = user->mRgbQuantizationRange;

	if (user->mPixelPackingDefaultPhase != 0)
		cfg->mPixelPackingDefaultPhase = user->mPixelPackingDefaultPhase;

	if (user->mColorimetry != (u8)(~0))
		cfg->mColorimetry = user->mColorimetry;

	if (user->mScanInfo != 0)
		cfg->mScanInfo = user->mScanInfo;

	if (user->mActiveFormatAspectRatio != 0)
		cfg->mActiveFormatAspectRatio = user->mActiveFormatAspectRatio;

	if (user->mNonUniformScaling != 0)
		cfg->mNonUniformScaling = user->mNonUniformScaling;

	if (user->mExtColorimetry != 0)
		cfg->mExtColorimetry = user->mExtColorimetry;

	if (user->mItContent != 0)
		cfg->mItContent = user->mItContent;

	if (user->mEndTopBar != (u16)(~0))
		cfg->mEndTopBar = user->mEndTopBar;

	if (user->mStartBottomBar != (u16)(~0))
		cfg->mStartBottomBar = user->mStartBottomBar;

	if (user->mEndLeftBar != 0)
		cfg->mEndLeftBar = user->mEndLeftBar;

	if (user->mStartRightBar != (u16)(~0))
		cfg->mStartRightBar = user->mStartRightBar;

	if (user->mCscFilter != 0)
		cfg->mCscFilter = user->mCscFilter;

	if ((user->mCscA[0] != 0) || (user->mCscA[1] != 0)
		|| (user->mCscA[2] != 0) || (user->mCscA[3] != 0))
		memcpy(cfg->mCscA, user->mCscA, sizeof(cfg->mCscA[0])*4);

	if ((user->mCscB[0] != 0) || (user->mCscB[1] != 0)
		|| (user->mCscB[2] != 0) || (user->mCscB[3] != 0))
		memcpy(cfg->mCscB, user->mCscB, sizeof(cfg->mCscB[0])*4);

	if ((user->mCscC[0] != 0) || (user->mCscC[1] != 0)
		|| (user->mCscC[2] != 0) || (user->mCscC[3] != 0))
		memcpy(cfg->mCscC, user->mCscC, sizeof(cfg->mCscC[0])*4);

	if (user->mCscScale != 0)
		cfg->mCscScale = user->mCscScale;

	if (user->mHdmiVideoFormat != 0)
		cfg->mHdmiVideoFormat = user->mHdmiVideoFormat;

	if (user->m3dStructure != 0)
		cfg->m3dStructure = user->m3dStructure;

	if (user->m3dExtData != 0)
		cfg->m3dExtData = user->m3dExtData;

	if (user->mHdmiVic != 0)
		cfg->mHdmiVic = user->mHdmiVic;

	update_dtd_cfg(&(user->mDtd), &(cfg->mDtd));
}

void video_apply(struct hdmi_tx_core *core)
{
	struct hdmi_mode *user_cfg = NULL;

	if (core == NULL) {
		HDMI_INFO_MSG("HDMI_ERROR:Improper arguments");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_INFO_MSG("HDMI_ERROR:Improper user_cfg pointer value");
		return;
	}

	/*if hdcp is not used, disable it*/
	if ((core->hdmi_tx.snps_hdmi_ctrl.hdcp_on == false)
			|| (core->mode.pHdcp.bypass == true)) {
		hdcp_rxdetect(&core->hdmi_tx, 0);
		hdcp_disable_encryption(&core->hdmi_tx, true);
		HDMI_INFO_MSG("HDCP Disabled\n");
	}

	/* Using YCC420 means that we are in HDMI 2.0 */
	if (core->mode.pVideo.mEncodingOut == YCC420) {
		core->mode.pVideo.mHdmi20 = 1;
		dev_write_mask(&core->hdmi_tx, (0x1032<<2), 0xff, 0);
		dev_write_mask(&core->hdmi_tx, (0x1033<<2), 0xff, 0);
	}


	if ((core->mode.sink_cap != NULL)
		&& (core->mode.sink_cap->edid_mHdmivsdb.mSupportsAi == 0)) {
		packets_StopSendIsrc1(&core->hdmi_tx);
		packets_StopSendIsrc2(&core->hdmi_tx);
	}
	print_videoinfo(&(core->mode.pVideo));
	if (!api_Configure(&core->hdmi_tx, &core->mode.pVideo,
		&core->mode.pAudio, &core->mode.pProduct,
			&core->mode.pHdcp, core->hdmi_tx_phy)) {
		HDMI_ERROR_MSG("\n");
	}
}

s32 set_static_config(struct disp_device_config *config)
{
	u32 data_bit = 0;
	struct hdmi_tx_core *core = get_platform();
	videoParams_t *pVideo = &core->mode.pVideo;
	LOG_TRACE();

	pr_info("[HDMI receive params]:  tv mode: %d   format:%d   data bits:%d  eotf: %d  cs: %d\n",
				config->mode, config->format, config->bits, config->eotf, config->cs);
	/*set vic mode and dtd*/
	hdmi_set_display_mode(config->mode);

	core_init_audio(&core->mode);
	/*set encoding mode*/
	pVideo->mEncodingIn = config->format;
	pVideo->mEncodingOut = config->format;

	/*set data bits*/
	if ((config->bits >= 0) && (config->bits < 3))
		data_bit = 8 + 2 * config->bits;
	if (config->bits == 3)
		data_bit = 16;
	pVideo->mColorResolution = (u8)data_bit;

	/*set eotf and color space*/
	if (config->eotf) {
		if (core->mode.pVideo.pb == NULL)
			pVideo->pb = kmalloc(sizeof(fc_drm_pb_t), GFP_KERNEL);
		else
			memset(pVideo->pb, 0, sizeof(fc_drm_pb_t));

		if (pVideo->pb == NULL)
			return -1;

		switch (config->eotf) {
		case DISP_EOTF_GAMMA22:
			pVideo->mHdr = 1;
			pVideo->pb->eotf = SDR_LUMINANCE_RANGE;
			break;
		case DISP_EOTF_SMPTE2084:
			pVideo->mHdr = 1;
			pVideo->pb->eotf = SMPTE_ST_2084;
			break;
		case DISP_EOTF_ARIB_STD_B67:
			pVideo->mHdr = 1;
			pVideo->pb->eotf = HLG;
			break;
		default:
			break;
		}

	} else {
		if (config->mode < 4)
			pVideo->mColorimetry = ITU601;
		else
			pVideo->mColorimetry = ITU709;

	}

	switch (config->cs) {
	case DISP_UNDEF:
		pVideo->mColorimetry = 0;
		break;
	case DISP_BT601:
		pVideo->mColorimetry = ITU601;
		break;
	case DISP_BT709:
		pVideo->mColorimetry = ITU709;
		break;
	case DISP_BT2020NC:
		pVideo->mColorimetry = EXTENDED_COLORIMETRY;
		pVideo->mExtColorimetry = BT2020_Y_CB_CR;
		break;
	default:
		pVideo->mColorimetry = 0;
		break;
	}

	return 0;
}

s32 get_static_config(struct disp_device_config *config)
{
	u32 i;
	enum disp_tv_mode tv_mode;
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();
	for (i = 0; i < sizeof(hdmi_mode_tbl)/sizeof(struct disp_hdmi_mode); i++) {
			if (hdmi_mode_tbl[i].hdmi_mode == core->mode.pVideo.mDtd.mCode) {
					tv_mode = hdmi_mode_tbl[i].mode;
					break;
			}
	}
	config->mode = tv_mode;

	config->format = core->mode.pVideo.mEncodingIn;

	if ((core->mode.pVideo.mColorResolution >= 8) && (core->mode.pVideo.mColorResolution < 16))
		config->bits = (core->mode.pVideo.mColorResolution - 8)/2;
	if (core->mode.pVideo.mColorResolution == 16)
		config->bits  = 4;

	if (core->mode.pVideo.pb != NULL) {

		switch (core->mode.pVideo.pb->eotf) {
		case SDR_LUMINANCE_RANGE:
			config->eotf = DISP_EOTF_GAMMA22;
			break;
		case SMPTE_ST_2084:
			config->eotf = DISP_EOTF_SMPTE2084;
			break;
		case HLG:
			config->eotf = DISP_EOTF_ARIB_STD_B67;
			break;
		default:
			break;
		}

		config->cs = DISP_BT2020NC;
	} else {
		if (tv_mode < 4)
			config->cs = DISP_BT601;
		else
			config->cs = DISP_BT709;

	}

	return 0;
}



s32 set_dynamic_config(struct disp_device_dynamic_config *config)
{
	void *hdr_buff_addr;
	u32 i = 0;
	u8 *temp = kmalloc(sizeof(config->metadata_size), GFP_KERNEL);
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();
	if (temp == NULL)
		return -1;

	hdr_buff_addr = config->vmap(config->metadata_buf, config->metadata_size);
	memcpy((void *)temp, hdr_buff_addr, config->metadata_size);
	config->vunmap(hdr_buff_addr);

	if (core->mode.pVideo.pb != NULL) {
		core->mode.pVideo.pb->eotf = bit_field(temp[0], 0, 3);
		core->mode.pVideo.pb->metadata = bit_field(temp[1], 0, 3);
		core->mode.pVideo.pb->r_x = temp[2];
		core->mode.pVideo.pb->r_y = temp[3];
		core->mode.pVideo.pb->g_x = temp[4];
		core->mode.pVideo.pb->g_y = temp[5];
		core->mode.pVideo.pb->b_x = temp[6];
		core->mode.pVideo.pb->b_y = temp[7];
		core->mode.pVideo.pb->luma_max = temp[8];
		core->mode.pVideo.pb->luma_min = temp[9];
	}

	HDMI_INFO_MSG("HDR info:\n");
	for (i = 0; i < config->metadata_size; i++)
		HDMI_INFO_MSG("data[%d]: %d\n", i, temp[i]);
	kfree(temp);

	return 0;
}

s32 get_dynamic_config(struct disp_device_dynamic_config *config)
{
	LOG_TRACE();
	return 0;
}

s32 hdmi_set_display_mode(u32 mode)
{
	u32 hdmi_mode;
	u32 i;
	bool find = false;

	HDMI_INFO_MSG("[hdmi_set_display_mode],mode:%d\n", mode);

	for (i = 0; i < sizeof(hdmi_mode_tbl)/sizeof(struct disp_hdmi_mode); i++) {
		if (hdmi_mode_tbl[i].mode == (enum disp_tv_mode)mode) {
			hdmi_mode = hdmi_mode_tbl[i].hdmi_mode;
			find = true;
			break;
		}
	}

	if (find) {
		/*user configure detailed timing according to vic*/
		if (svd_user_config(hdmi_mode, 0) != 0) {
			HDMI_ERROR_MSG("svd user config failed!\n");
			return -1;
		} else {
			HDMI_INFO_MSG("Set hdmi mode: %d\n", hdmi_mode);
			return 0;
		}
	} else {
		HDMI_ERROR_MSG("unsupported video mode %d when set display mode\n", mode);
		return -1;
	}

}

s32 hdmi_mode_support(u32 mode)
{
	u32 hdmi_mode;
	u32 i;
	bool find = false;

	LOG_TRACE();
	for (i = 0; i < sizeof(hdmi_mode_tbl)/sizeof(struct disp_hdmi_mode); i++) {
		if (hdmi_mode_tbl[i].mode == (enum disp_tv_mode)mode) {
			hdmi_mode = hdmi_mode_tbl[i].hdmi_mode;
			find = true;
			break;
		}
	}

	if (find)
		return edid_sink_supports_vic_code(hdmi_mode);
	else
		return 0;
}

s32 hdmi_get_HPD_status(void)
{
	LOG_TRACE();
	return hdmi_core_get_hpd_state();
}


s32 hdmi_core_get_csc_type(void)
{
	u32 set_encoding = 0;
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();

	set_encoding = core->mode.pVideo.mEncodingIn;
	HDMI_INFO_MSG("set encode=%d\n", set_encoding);

	return set_encoding;
}


s32 hdmi_get_video_timming_info(struct disp_video_timings **video_info)
{
	dtd_t *dtd = NULL;
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();

	dtd = &core->mode.pVideo.mDtd;
	info.vic = dtd->mCode;
	info.tv_mode = 0;

	info.pixel_clk = (dtd->mPixelClock) * 1000 / (dtd->mPixelRepetitionInput+1);
	if ((info.vic == 6) || (info.vic == 21))
		info.pixel_clk = (dtd->mPixelClock) * 1000 / (dtd->mPixelRepetitionInput + 1) / (dtd->mInterlaced + 1);
	info.pixel_repeat = dtd->mPixelRepetitionInput;
	info.x_res = (dtd->mHActive) / (dtd->mPixelRepetitionInput+1);
	if (dtd->mInterlaced == 1)
		info.y_res = (dtd->mVActive) * 2;
	else if (dtd->mInterlaced == 0)
		info.y_res = dtd->mVActive;

	info.hor_total_time = (dtd->mHActive + dtd->mHBlanking) / (dtd->mPixelRepetitionInput+1);
	info.hor_back_porch = (dtd->mHBlanking - dtd->mHSyncOffset - dtd->mHSyncPulseWidth) / (dtd->mPixelRepetitionInput+1);
	info.hor_front_porch = (dtd->mHSyncOffset) / (dtd->mPixelRepetitionInput+1);
	info.hor_sync_time = (dtd->mHSyncPulseWidth) / (dtd->mPixelRepetitionInput+1);

	if (dtd->mInterlaced == 1)
		info.ver_total_time = (dtd->mVActive + dtd->mVBlanking) * 2 + 1;
	else if (dtd->mInterlaced == 0)
		info.ver_total_time = dtd->mVActive + dtd->mVBlanking;
	info.ver_back_porch = dtd->mVBlanking - dtd->mVSyncOffset - dtd->mVSyncPulseWidth;
	info.ver_front_porch = dtd->mVSyncOffset;
	info.ver_sync_time = dtd->mVSyncPulseWidth;

	info.hor_sync_polarity = dtd->mHSyncPolarity;
	info.ver_sync_polarity = dtd->mVSyncPolarity;
	info.b_interlace = dtd->mInterlaced;

	if (dtd->mCode == HDMI1080P_24_3D_FP) {
		info.y_res = (dtd->mVActive) * 2;
		info.vactive_space = 45;
		info.trd_mode = 1;
	} else if (dtd->mCode == HDMI720P_50_3D_FP) {
		info.y_res = (dtd->mVActive) * 2;
		info.vactive_space = 30;
		info.trd_mode = 1;
	} else if (dtd->mCode == HDMI720P_60_3D_FP) {
		info.y_res = (dtd->mVActive) * 2;
		info.vactive_space = 30;
		info.trd_mode = 1;
	} else {
		info.vactive_space = 0;
		info.trd_mode = 0;
	}

	*video_info = &info;
	return 0;
}

s32 hdmi_enable_core(void)
{
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();

	/* Disable all interrupts */
	irq_mask_all(&core->hdmi_tx);

	/* Print Core version */
	HDMI_INFO_MSG("HDMI CORE version = %x\n",
		(id_design(&core->hdmi_tx) << 8) | id_revision(&core->hdmi_tx));

	irq_hpd_sense_enable(&core->hdmi_tx);

	if (core->hpd_state < HDMI_State_Rx_Sense)
		return 0;

	video_apply(core);
	irq_unmute_source(&p_core->hdmi_tx, PHY);

	return 0;
}

s32 hdmi_disable_core(void)
{
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();

	/*step2:Close HDCP*/
	if (core->hdmi_tx.snps_hdmi_ctrl.hdcp_on > 0) {
		hdcp_rxdetect(&core->hdmi_tx, 0);
		hdcp_disable_encryption(&core->hdmi_tx, true);
	}

	return 0;
}

s32 hdmi_suspend_core(void)
{
	struct hdmi_tx_core *core = get_platform();

	LOG_TRACE();

	phy_disable_hpd_sense(&core->hdmi_tx);

	irq_mask_bit(&core->hdmi_tx, PHY, IH_MUTE_PHY_STAT0_HPD_MASK);
	irq_mute(&core->hdmi_tx);

	api_Standby(&core->hdmi_tx);
	hdcp_av_mute(&core->hdmi_tx, 1);

	/*step:disable clock*/
	/* clk_disable(hdmi_clk); */

	return 0;
}

s32 hdmi_resume_core(void)
{
	/* u32 ret = 0; */
	struct hdmi_tx_core *core = get_platform();

	phy_enable_hpd_sense(&core->hdmi_tx);
	irq_unmask_bit(&core->hdmi_tx, PHY, IH_MUTE_PHY_STAT0_HPD_MASK);
	irq_unmute(&core->hdmi_tx);
	if (!api_Configure(&core->hdmi_tx, &core->mode.pVideo,
			&core->mode.pAudio, &core->mode.pProduct,
			&core->mode.pHdcp, core->hdmi_tx_phy)) {

		HDMI_ERROR_MSG("api configure failed\n");
	}
	/*step:enable clock*/
	/* ret = clk_prepare_enable(hdmi_clk); */

	return 0;
}

/***********************************************************/
/*************************HDCP******************************/
/**********************************************************/
/*
*@enable: 1-enable
*		    0-disable
*/
void hdcp_core_enable(u8 enable)
{
	struct hdmi_tx_core *core = get_platform();

	if (core == NULL) {
		HDMI_ERROR_MSG("Empty structure\n");
		return;
	}

	if (enable) {
		core->hdmi_tx.snps_hdmi_ctrl.hdcp_on = true;
		core->mode.pHdcp.bypass = false;
		HDMI_INFO_MSG("HDCP Enable\n");
	} else if (enable == 0) {
		core->hdmi_tx.snps_hdmi_ctrl.hdcp_on = false;
		core->mode.pHdcp.bypass = true;
		hdcp_rxdetect(&core->hdmi_tx, 0);
		hdcp_disable_encryption(&core->hdmi_tx, true);
		HDMI_INFO_MSG("HDCP Disabled\n");
	} else{
		HDMI_ERROR_MSG("Error hdcp enable prameter\n");
		return;
	}

	/* mutex_lock(&(core->my_mutex)); */
	/*please don't run a total api configure again,
		we just only  need run hdcp configure*/
	api_Configure(&core->hdmi_tx, &core->mode.pVideo,
			&core->mode.pAudio, &core->mode.pProduct,
			&core->mode.pHdcp, core->hdmi_tx_phy);
	irq_unmute_source(&core->hdmi_tx, PHY);
	/* mutex_unlock(&(core->my_mutex)); */
}


/***********************************************************/
/*************************Audio******************************/
/**********************************************************/
/*
*@coding_type: PCM=1,AC3=2,MPEG1=3,MPEG2=4,MP3=5,
		      AAC=6,DTS=7,ATRAC=8,ONE_BIT_AUDIO=9,
		      DOLBY_DIGITAL_PLUS=10,DTS_HD=11
*/
void coding_type_user_config(u32 coding_type)
{
	u32 type = coding_type;
	struct hdmi_tx_core *core = get_platform();
	struct hdmi_mode *user_cfg = NULL;

	if (core == NULL) {
		HDMI_ERROR_MSG("No useful struct\n");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	if ((type < 1) || (type > 11)) {
		HDMI_ERROR_MSG("Type [%d] is not supported\n", type);
		return;
	}

	user_cfg->pAudio.mCodingType = type;
	audio_corely();
}

/*
frequency choise:
32 000Hz,     44 100Hz,    48 000Hz
64 000Hz,     88 200Hz,    96 000Hz
128 000Hz,    176 400Hz,   192 000Hz
256 000Hz,    352 800Hz,   512 000Hz
705 600Hz,    768 000Hz,   10 240 000Hz
1 411 200Hz,  1 536 000Hz
*/
void sample_freq_user_config(u32 freq)
{
	u32 sample_freq = freq;
	struct hdmi_mode *user_cfg = NULL;
	struct hdmi_tx_core *core = get_platform();

	if (core == NULL) {
		HDMI_ERROR_MSG("No useful core struct\n");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	if ((sample_freq == 0) || (sample_freq < 0)) {
		HDMI_ERROR_MSG("Frequency %dHz is not supported\n",
							sample_freq);
		return;
	}

	if (sample_freq > 1536000) {
		HDMI_ERROR_MSG("Frequency %dHz is higher than supported\n",
								sample_freq);
	}

	user_cfg->pAudio.mSamplingFrequency = sample_freq;
	audio_corely();
}

/*
*@type=0-I2S,
*		 1-SPDIF
*		 2-HBR,
*		 3-GPA,
*		 4-DMA
*/
void interface_type_user_config(u32 type)
{
	struct hdmi_mode *user_cfg = NULL;
	struct hdmi_tx_core *core = get_platform();

	if (core == NULL) {
		HDMI_ERROR_MSG("No useful core struct\n");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	if ((type < 0) || (type > 4))
		HDMI_ERROR_MSG("Improper audio interface type\n");
	else
		user_cfg->pAudio.mInterfaceType = type;

	audio_corely();
}

/*
*@size: <16-24>
*/
void sample_size_user_config(u32 size)
{
	u32 sample_size = size;
	struct hdmi_mode *user_cfg = NULL;
	struct hdmi_tx_core *core = get_platform();

	if (core == NULL) {
		HDMI_ERROR_MSG("No useful core struct\n");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	if ((sample_size < 16) || (sample_size > 24)) {
		HDMI_ERROR_MSG("Sample size of %d is not supported\n",
								sample_size);
		return;
	}
	user_cfg->pAudio.mSampleSize = sample_size;
	audio_corely();
}

/*
*@fs_factor: 64,128,256,512
*fs_factor*fs=ftmds_clock*N/CTS
*/
void fs_factor_user_config(u32 fs_factor)
{
	u32 fsfactor = fs_factor;
	struct hdmi_mode *user_cfg = NULL;
	struct hdmi_tx_core *core = get_platform();

	if (core == NULL) {
		HDMI_ERROR_MSG("No useful core struct\n");
		return;
	}

	user_cfg = core_get_user_config(core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	if ((fsfactor != 64) && (fsfactor != 128)
		&& (fsfactor != 256) && (fsfactor != 512)) {
		HDMI_ERROR_MSG("FS factor %d is not supported\n", fsfactor);
		return;
	}
	user_cfg->pAudio.mClockFsFactor = fsfactor;

	audio_corely();
}

void update_audio_cfg(audioParams_t *user, audioParams_t *cfg)
{
	if (user->mInterfaceType != INTERFACE_NOT_DEFINED)
		cfg->mInterfaceType = user->mInterfaceType;

	if (user->mCodingType != CODING_NOT_DEFINED)
		cfg->mCodingType = user->mCodingType;

	if (user->mChannelAllocation != 0)
		cfg->mChannelAllocation = user->mChannelAllocation;

	if (user->mSampleSize != 0)
		cfg->mSampleSize = user->mSampleSize;

	if (user->mSamplingFrequency)
		cfg->mSamplingFrequency = user->mSamplingFrequency;

	if (user->mLevelShiftValue != 0)
		cfg->mLevelShiftValue = user->mLevelShiftValue;

	if (user->mDownMixInhibitFlag != 0)
		cfg->mDownMixInhibitFlag = user->mDownMixInhibitFlag;

	if (user->mIecCopyright != 0)
		cfg->mIecCopyright = user->mIecCopyright;

	if (user->mIecCgmsA != 0)
		cfg->mIecCgmsA = user->mIecCgmsA;

	if (user->mIecPcmMode != 0)
		cfg->mIecPcmMode = user->mIecPcmMode;

	if (user->mIecCategoryCode != 0)
		cfg->mIecCategoryCode = user->mIecCategoryCode;

	if (user->mIecSourceNumber != 0)
		cfg->mIecSourceNumber = user->mIecSourceNumber;

	if (user->mIecClockAccuracy != 0)
		cfg->mIecClockAccuracy = user->mIecClockAccuracy;

	if (user->mPacketType != PACKET_NOT_DEFINED)
		cfg->mPacketType = user->mPacketType;

	if (user->mClockFsFactor != 0)
		cfg->mClockFsFactor = user->mClockFsFactor;

	if (user->mDmaBeatIncrement != DMA_NOT_DEFINED)
		cfg->mDmaBeatIncrement = user->mDmaBeatIncrement;

	if (user->mDmaThreshold != 0)
		cfg->mDmaThreshold = user->mDmaThreshold;

	if (user->mDmaHlock != 0)
		cfg->mDmaHlock = user->mDmaHlock;

	if (user->mGpaInsertPucv != 0)
		cfg->mGpaInsertPucv = user->mGpaInsertPucv;

	if (user->mAudioMetaDataPacket.mAudioMetaDataHeader.m3dAudio != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataHeader.m3dAudio = user->mAudioMetaDataPacket.mAudioMetaDataHeader.m3dAudio;

	if (user->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumAudioStreams != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumAudioStreams = user->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumAudioStreams;

	if (user->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumViews != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumViews = user->mAudioMetaDataPacket.mAudioMetaDataHeader.mNumViews;

	if (user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mLC_Valid != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mLC_Valid = user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mLC_Valid;

	if (user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mMultiviewRightLeft != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mMultiviewRightLeft = user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mMultiviewRightLeft;

	if (user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Mixed != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Mixed = user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Mixed;

	if (user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Valid != 0)
		cfg->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Valid = user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Valid;

	if (user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Type != RESERVED)
		cfg->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Type = user->mAudioMetaDataPacket.mAudioMetaDataDescriptor->mSuppl_A_Type;
}

void audio_corely(void)
{
	struct hdmi_mode *user_cfg = NULL;
	struct hdmi_tx_core *p_core = get_platform();

	if (p_core == NULL) {
		HDMI_ERROR_MSG("Improper arguments\n");
		return;
	}

	user_cfg = core_get_user_config(p_core);

	if (user_cfg == NULL) {
		HDMI_ERROR_MSG("Improper user_cfg pointer value\n");
		return;
	}

	update_audio_cfg(&(user_cfg->pAudio), &(p_core->mode.pAudio));


	if (!api_Configure(&(p_core->hdmi_tx), &(p_core->mode.pVideo),
		&(p_core->mode.pAudio), &(p_core->mode.pProduct),
		&(p_core->mode.pHdcp), p_core->hdmi_tx_phy)) {
		HDMI_ERROR_MSG("API Configure ERROR\n");
	}

	p_core->user_cfg = NULL;
	kfree(user_cfg);
}

#if defined(CONFIG_SND_SUNXI_SOC_HDMIAUDIO)
s32 hdmi_core_audio_enable(u8 enable, u8 channel)
{
	struct hdmi_tx_core *p_core = get_platform();

	LOG_TRACE();

	HDMI_INFO_MSG("mode:%d   channel:%d\n", enable, channel);
	p_core->hdmi_tx.snps_hdmi_ctrl.audio_on = 1;
	if (p_core->hpd_state < HDMI_State_Rx_Sense || enable == 0 || audio_start_config == 1) {
		audio_start_config = 0;
		return 0;
	}
	print_audioinfo(&(p_core->mode.pAudio));
	if (!api_audio_configure(&(p_core->hdmi_tx), &(p_core->mode.pAudio))) {
		HDMI_ERROR_MSG("API Configure ERROR\n");
		return -1;
	} else {
		irq_unmute_source(&p_core->hdmi_tx, PHY);
		return 0;
	}
}

s32 hdmi_set_audio_para(hdmi_audio_t *audio_para)
{
	struct hdmi_tx_core *p_core = get_platform();
	audioParams_t *pAudio = &p_core->mode.pAudio;

	LOG_TRACE();

	if (audio_para->hw_intf < 2)
		pAudio->mInterfaceType = audio_para->hw_intf;
	else
		pr_info("Unknow hardware interface type\n");
	pAudio->mCodingType = audio_para->data_raw;
	pAudio->mSamplingFrequency = audio_para->sample_rate;
	pAudio->mChannelAllocation = audio_para->ca;
	pAudio->mChannelNum = audio_para->channel_num;
	pAudio->mSampleSize = audio_para->sample_bit;
	pAudio->mClockFsFactor = audio_para->fs_between;

	return 0;
}
#endif
