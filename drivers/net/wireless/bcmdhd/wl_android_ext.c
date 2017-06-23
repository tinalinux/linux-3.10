

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>

#include <wl_android.h>
#include <wldev_common.h>
#include <wlioctl.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_config.h>

#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i

#define CMD_GET_CHANNEL			"GET_CHANNEL"
#define CMD_SET_CHANNEL			"SET_CHANNEL"
#define CMD_SET_ROAM			"SET_ROAM_TRIGGER"
#define CMD_GET_ROAM			"GET_ROAM_TRIGGER"
#define CMD_GET_KEEP_ALIVE		"GET_KEEP_ALIVE"
#define CMD_GET_PM				"GET_PM"
#define CMD_SET_PM				"SET_PM"
#define CMD_MONITOR				"MONITOR"
#ifdef WL_EXT_IAPSTA
#define CMD_STA_INIT			"STA_INIT"
#define CMD_STA_START			"STA_START"
#define CMD_STA_STOP			"STA_STOP"
#define CMD_AP_INIT				"AP_INIT"
#define CMD_AP_START			"AP_START"
#define CMD_AP_STOP				"AP_STOP"
#define CMD_APSTA_INIT			"APSTA_INIT"
#define CMD_APSTA_START			"APSTA_START"
#define CMD_APSTA_STOP			"APSTA_STOP"

#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#define IEEE80211_BAND_2GHZ 0
#define IEEE80211_BAND_5GHZ 1
#endif

#ifdef WL_EXT_IAPSTA
int wl_ext_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len, u32 set)
{
	int ret;

	ret = wldev_ioctl(dev, cmd, arg, len, set);
	if (ret)
		ANDROID_ERROR(("%s: cmd=%d ret=%d\n", __FUNCTION__, cmd, ret));
	return ret;
}

int wl_ext_iovar_setint(struct net_device *dev, s8 *iovar, s32 val)
{
	int ret;

	ret = wldev_iovar_setint(dev, iovar, val);
	if (ret)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar, ret));

	return ret;
}

int wl_ext_iovar_setbuf(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf(dev, iovar_name, param, paramlen, buf, buflen, buf_sync);
	if (ret != 0)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}

int wl_ext_iovar_setint_bsscfg(struct net_device *dev, s8 *iovar,
	s32 val, s32 bssidx)
{
	int ret;

	ret = wldev_iovar_setint_bsscfg(dev, iovar, val, bssidx);
	if (ret < 0)
		ANDROID_ERROR(("%s: iovar=%s, ret=%d\n", __FUNCTION__, iovar, ret));

	return ret;
}

int wl_ext_iovar_setbuf_bsscfg(struct net_device *dev, s8 *iovar_name,
	void *param, s32 paramlen, void *buf, s32 buflen, s32 bsscfg_idx, struct mutex* buf_sync)
{
	int ret;

	ret = wldev_iovar_setbuf_bsscfg(dev, iovar_name, param, paramlen,
		buf, buflen, bsscfg_idx, buf_sync);
	if (ret < 0)
		ANDROID_ERROR(("%s: iovar_name=%s ret=%d\n", __FUNCTION__, iovar_name, ret));

	return ret;
}
#endif

/* Return a legacy chanspec given a new chanspec
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_ext_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		ANDROID_ERROR(("wl_ext_chspec_to_legacy: input chanspec (0x%04X) malformed\n",
		        chspec));
		return INVCHANSPEC;
	}

	/* get the channel number */
	lchspec = CHSPEC_CHANNEL(chspec);

	/* convert the band */
	if (CHSPEC_IS2G(chspec)) {
		lchspec |= WL_LCHANSPEC_BAND_2G;
	} else {
		lchspec |= WL_LCHANSPEC_BAND_5G;
	}

	/* convert the bw and sideband */
	if (CHSPEC_IS20(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_20;
		lchspec |= WL_LCHANSPEC_CTL_SB_NONE;
	} else if (CHSPEC_IS40(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_40;
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_L) {
			lchspec |= WL_LCHANSPEC_CTL_SB_LOWER;
		} else {
			lchspec |= WL_LCHANSPEC_CTL_SB_UPPER;
		}
	} else {
		/* cannot express the bandwidth */
		char chanbuf[CHANSPEC_STR_LEN];
		ANDROID_ERROR((
		        "wl_ext_chspec_to_legacy: unable to convert chanspec %s (0x%04X) "
		        "to pre-11ac format\n",
		        wf_chspec_ntoa(chspec, chanbuf), chspec));
		return INVCHANSPEC;
	}

	return lchspec;
}

/* given a chanspec value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_ext_chspec_host_to_driver(int ioctl_ver, chanspec_t chanspec)
{
	if (ioctl_ver == 1) {
		chanspec = wl_ext_chspec_to_legacy(chanspec);
		if (chanspec == INVCHANSPEC) {
			return chanspec;
		}
	}
	chanspec = htodchanspec(chanspec);

	return chanspec;
}

static int
wl_ext_get_ioctl_ver(struct net_device *dev, int *ioctl_ver)
{
	int ret = 0;
	s32 val = 0;

	val = 1;
	ret = wl_ext_ioctl(dev, WLC_GET_VERSION, &val, sizeof(val), 0);
	if (ret) {
		ANDROID_ERROR(("WLC_GET_VERSION failed, err=%d\n", ret));
		return ret;
	}
	val = dtoh32(val);
	if (val != WLC_IOCTL_VERSION && val != 1) {
		ANDROID_ERROR(("Version mismatch, please upgrade. Got %d, expected %d or 1\n",
			val, WLC_IOCTL_VERSION));
		return BCME_VERSION;
	}
	*ioctl_ver = val;
	printf("%s: ioctl_ver=%d\n", __FUNCTION__, val);

	return ret;
}

int
wl_ext_get_channel(
struct net_device *dev, char* command, int total_len)
{
	int ret;
	channel_info_t ci;
	int bytes_written = 0;

	if (!(ret = wldev_ioctl(dev, WLC_GET_CHANNEL, &ci, sizeof(channel_info_t), FALSE))) {
		ANDROID_TRACE(("hw_channel %d\n", ci.hw_channel));
		ANDROID_TRACE(("target_channel %d\n", ci.target_channel));
		ANDROID_TRACE(("scan_channel %d\n", ci.scan_channel));
		bytes_written = snprintf(command, sizeof(channel_info_t)+2, "channel %d", ci.hw_channel);
		ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
	}

	return bytes_written;
}

static int
wl_ext_set_chanspec(struct net_device *dev, uint16 channel)
{
	s32 _chan = channel;
	chanspec_t chspec = 0;
	chanspec_t fw_chspec = 0;
	u32 bw = WL_CHANSPEC_BW_20;
	s32 err = BCME_OK;
	s32 bw_cap = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	uint band;
	int ioctl_ver;

	if (_chan <= CH_MAX_2G_CHANNEL)
		band = IEEE80211_BAND_2GHZ;
	else
		band = IEEE80211_BAND_5GHZ;
	wl_ext_get_ioctl_ver(dev, &ioctl_ver);

	if (band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wldev_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				ANDROID_ERROR(("bw_cap failed, %d\n", err));
				return err;
			} else {
				err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (err) {
					ANDROID_ERROR(("error get mimo_bw_cap (%d)\n", err));
				}
				if (bw_cap != WLC_N_BW_20ALL)
					bw = WL_CHANSPEC_BW_40;
			}
		} else {
			if (WL_BW_CAP_80MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_80;
			else if (WL_BW_CAP_40MHZ(iovar_buf[0]))
				bw = WL_CHANSPEC_BW_40;
			else
				bw = WL_CHANSPEC_BW_20;

		}
	}
	else if (band == IEEE80211_BAND_2GHZ)
		bw = WL_CHANSPEC_BW_20;

set_channel:
	chspec = wf_channel2chspec(_chan, bw);
	if (wf_chspec_valid(chspec)) {
		fw_chspec = wl_ext_chspec_host_to_driver(ioctl_ver, chspec);
		if (fw_chspec != INVCHANSPEC) {
			if ((err = wldev_iovar_setint(dev, "chanspec", fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				wl_ext_ioctl(dev, WLC_SET_CHANNEL, &_chan, sizeof(_chan), 1);
				printf("%s: channel %d\n", __FUNCTION__, _chan);
			} else if (err) {
				ANDROID_ERROR(("%s: failed to set chanspec error %d\n", __FUNCTION__, err));
			} else
				printf("%s: channel %d, 0x%x\n", __FUNCTION__, channel, chspec);
		} else {
			ANDROID_ERROR(("%s: failed to convert host chanspec to fw chanspec\n", __FUNCTION__));
			err = BCME_ERROR;
		}
	} else {
change_bw:
		if (bw == WL_CHANSPEC_BW_80)
			bw = WL_CHANSPEC_BW_40;
		else if (bw == WL_CHANSPEC_BW_40)
			bw = WL_CHANSPEC_BW_20;
		else
			bw = 0;
		if (bw)
			goto set_channel;
		ANDROID_ERROR(("%s: Invalid chanspec 0x%x\n", __FUNCTION__, chspec));
		err = BCME_ERROR;
	}

	return err;
}

int
wl_ext_set_channel(
struct net_device *dev, char* command, int total_len)
{
	int ret;
	int bytes_written = 0;
	int channel;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &channel);

	ret = wl_ext_set_chanspec(dev, channel);

	return bytes_written;
}

int
wl_ext_set_roam_trigger(
struct net_device *dev, char* command, int total_len)
{
	int ret = 0;
	int roam_trigger[2];

	sscanf(command, "%*s %10d", &roam_trigger[0]);
	roam_trigger[1] = WLC_BAND_ALL;

	ret = wldev_ioctl(dev, WLC_SET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 1);
	if (ret)
		ANDROID_ERROR(("WLC_SET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	return ret;
}

int
wl_ext_get_roam_trigger(
struct net_device *dev, char *command, int total_len)
{
	int ret;
	int bytes_written;
	int roam_trigger[2] = {0, 0};
	int trigger[2]= {0, 0};

	roam_trigger[1] = WLC_BAND_2G;
	ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
	if (!ret)
		trigger[0] = roam_trigger[0];
	else
		ANDROID_ERROR(("2G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	roam_trigger[1] = WLC_BAND_5G;
	ret = wldev_ioctl(dev, WLC_GET_ROAM_TRIGGER, roam_trigger, sizeof(roam_trigger), 0);
	if (!ret)
		trigger[1] = roam_trigger[0];
	else
		ANDROID_ERROR(("5G WLC_GET_ROAM_TRIGGER ERROR %d ret=%d\n", roam_trigger[0], ret));

	ANDROID_TRACE(("roam_trigger %d %d\n", trigger[0], trigger[1]));
	bytes_written = snprintf(command, total_len, "%d %d", trigger[0], trigger[1]);

	return bytes_written;
}

s32
wl_ext_get_keep_alive(struct net_device *dev, char *command, int total_len) {

	wl_mkeep_alive_pkt_t *mkeep_alive_pktp;
	int bytes_written = -1;
	int res = -1, len, i = 0;
	char* str = "mkeep_alive";

	ANDROID_TRACE(("%s: command = %s\n", __FUNCTION__, command));

	len = WLC_IOCTL_MEDLEN;
	mkeep_alive_pktp = kmalloc(len, GFP_KERNEL);
	memset(mkeep_alive_pktp, 0, len);
	strcpy((char*)mkeep_alive_pktp, str);

	if ((res = wldev_ioctl(dev, WLC_GET_VAR, mkeep_alive_pktp, len, FALSE))<0) {
		ANDROID_ERROR(("%s: GET mkeep_alive ERROR %d\n", __FUNCTION__, res));
		goto exit;
	} else {
		printf("Id            :%d\n"
			   "Period (msec) :%d\n"
			   "Length        :%d\n"
			   "Packet        :0x",
			   mkeep_alive_pktp->keep_alive_id,
			   dtoh32(mkeep_alive_pktp->period_msec),
			   dtoh16(mkeep_alive_pktp->len_bytes));
		for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
			printf("%02x", mkeep_alive_pktp->data[i]);
		}
		printf("\n");
	}
	bytes_written = snprintf(command, total_len, "mkeep_alive_period_msec %d ", dtoh32(mkeep_alive_pktp->period_msec));
	bytes_written += snprintf(command+bytes_written, total_len, "0x");
	for (i=0; i<mkeep_alive_pktp->len_bytes; i++) {
		bytes_written += snprintf(command+bytes_written, total_len, "%x", mkeep_alive_pktp->data[i]);
	}
	ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));

exit:
	kfree(mkeep_alive_pktp);
	return bytes_written;
}

int
wl_ext_set_pm(struct net_device *dev,char *command, int total_len)
{
	int pm, ret = -1;

	ANDROID_TRACE(("%s: cmd %s\n", __FUNCTION__, command));

	sscanf(command, "%*s %d", &pm);

	ret = wldev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm), FALSE);
	if (ret)
		ANDROID_ERROR(("WLC_SET_PM ERROR %d ret=%d\n", pm, ret));

	return ret;
}

int
wl_ext_get_pm(struct net_device *dev,char *command, int total_len)
{

	int ret = 0;
	int pm_local;
	char *pm;
	int bytes_written=-1;

	ret = wldev_ioctl(dev, WLC_GET_PM, &pm_local, sizeof(pm_local),FALSE);
	if (!ret) {
		ANDROID_TRACE(("%s: PM = %d\n", __func__, pm_local));
		if (pm_local == PM_OFF)
			pm = "PM_OFF";
		else if(pm_local == PM_MAX)
			pm = "PM_MAX";
		else if(pm_local == PM_FAST)
			pm = "PM_FAST";
		else {
			pm_local = 0;
			pm = "Invalid";
		}
		bytes_written = snprintf(command, total_len, "PM %s", pm);
		ANDROID_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
	}
	return bytes_written;
}

static int
wl_ext_set_monitor(struct net_device *dev, char *command, int total_len)
{
	int val, ret = -1;

	sscanf(command, "%*s %d", &val);
	ret = wldev_ioctl(dev, WLC_SET_MONITOR, &val, sizeof(int), 1);
	if (ret)
		ANDROID_ERROR(("WLC_SET_MONITOR ERROR %d ret=%d\n", val, ret));
	return ret;
}

#ifdef WL_EXT_IAPSTA
struct wl_apsta_params g_apsta_params;
static int
wl_ext_parse_wep(char *key, struct wl_wsec_key *wsec_key)
{
	char hex[] = "XX";
	unsigned char *data = wsec_key->data;
	char *keystr = key;

	switch (strlen(keystr)) {
	case 5:
	case 13:
	case 16:
		wsec_key->len = strlen(keystr);
		memcpy(data, keystr, wsec_key->len + 1);
		break;
	case 12:
	case 28:
	case 34:
	case 66:
		/* strip leading 0x */
		if (!strnicmp(keystr, "0x", 2))
			keystr += 2;
		else
			return -1;
		/* fall through */
	case 10:
	case 26:
	case 32:
	case 64:
		wsec_key->len = strlen(keystr) / 2;
		while (*keystr) {
			strncpy(hex, keystr, 2);
			*data++ = (char) strtoul(hex, NULL, 16);
			keystr += 2;
		}
		break;
	default:
		return -1;
	}

	switch (wsec_key->len) {
	case 5:
		wsec_key->algo = CRYPTO_ALGO_WEP1;
		break;
	case 13:
		wsec_key->algo = CRYPTO_ALGO_WEP128;
		break;
	case 16:
		/* default to AES-CCM */
		wsec_key->algo = CRYPTO_ALGO_AES_CCM;
		break;
	case 32:
		wsec_key->algo = CRYPTO_ALGO_TKIP;
		break;
	default:
		return -1;
	}

	/* Set as primary wsec_key by default */
	wsec_key->flags |= WL_PRIMARY_KEY;

	return 0;
}

static int
wl_ext_set_bgnmode(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	bgnmode_t bgnmode=apsta_params->bgnmode;
	int val;

	if (bgnmode == 0)
		return 0;

	wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
	if (bgnmode == IEEE80211B) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 0;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		printf("%s: Network mode: B only\n", __FUNCTION__);
	} else if (bgnmode == IEEE80211G) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 2;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		printf("%s: Network mode: G only\n", __FUNCTION__);
	} else if (bgnmode == IEEE80211BG) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		printf("%s: Network mode: : B/G mixed\n", __FUNCTION__);
	} else if (bgnmode == IEEE80211BGN) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		printf("%s: Network mode: : B/G/N mixed\n", __FUNCTION__);
	} else if (bgnmode == IEEE80211BGNAC) {
		wl_ext_iovar_setint(dev, "nmode", 0);
		wl_ext_iovar_setint(dev, "nmode", 1);
		wl_ext_iovar_setint(dev, "vhtmode", 1);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_GMODE, &val, sizeof(val), 1);
		printf("%s: Network mode: : B/G/N/AC mixed\n", __FUNCTION__);
	}
	wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);

	return 0;
}

static int
wl_ext_set_amode(struct net_device *dev, struct wl_apsta_params *apsta_params,
	s32 bssidx)
{
	authmode_t amode = apsta_params->amode;
	int sup_wpa=0, auth=0, wpa_auth=0;

	if (amode == AUTH_OPEN) {
		auth = 0;
		sup_wpa = 0;
		wpa_auth = 0;
		printf("%s: Authentication: Open System\n", __FUNCTION__);
	} else if (amode == AUTH_SHARED) {
		sup_wpa = 1;
		auth = 1;
		wpa_auth = 0;
		printf("%s: Authentication: Shared Key\n", __FUNCTION__);
	} else if (amode == AUTH_WPAPSK) {
		sup_wpa = 1;
		auth = 0;
		wpa_auth = 4;
		printf("%s: Authentication: WPA-PSK\n", __FUNCTION__);
	} else if (amode == AUTH_WPA2PSK) {
		sup_wpa = 1;
		auth = 0;
		wpa_auth = 128;
		printf("%s: Authentication: WPA2-PSK\n", __FUNCTION__);
	} else if (amode == AUTH_WPAWPA2PSK) {
		sup_wpa = 1;
		auth = 0;
		wpa_auth = 132;
		printf("%s: Authentication: WPA/WPA2-PSK\n", __FUNCTION__);
	}
	wl_ext_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (apsta_params->apstamode == IAP_MODE) // fix for 43455
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
	wl_ext_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (apsta_params->apstamode == IAP_MODE) // fix for 43455
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);;
	if (apsta_params->apstamode == ISTA_MODE)
		wl_ext_iovar_setint_bsscfg(dev, "sup_wpa", sup_wpa, bssidx);

	return 0;
}

static int
wl_ext_set_emode(struct net_device *dev, struct wl_apsta_params *apsta_params,
	s32 bssidx)
{
	int wsec=0;
	struct wl_wsec_key wsec_key;
	wsec_pmk_t psk;
	encmode_t emode = apsta_params->emode;
	char *key = apsta_params->key;
	s8 iovar_buf[WLC_IOCTL_SMLEN];

	memset(&wsec_key, 0, sizeof(wsec_key));
	memset(&psk, 0, sizeof(psk));
	if (emode == ENC_NONE) {
		wsec = 0;
		printf("%s: Encryption: No securiy\n", __FUNCTION__);
	} else if (emode == ENC_WEP) {
		wsec = 1;
		wl_ext_parse_wep(key, &wsec_key);
		printf("%s: Encryption: WEP\n", __FUNCTION__);
		printf("%s: Key: %s\n", __FUNCTION__, wsec_key.data);
	} else if (emode == ENC_TKIP) {
		wsec = 2;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		printf("%s: Encryption: TKIP\n", __FUNCTION__);
		printf("%s: Key: %s\n", __FUNCTION__, psk.key);
	} else if (emode == ENC_AES) {
		wsec = 4;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		printf("%s: Encryption: AES\n", __FUNCTION__);
		printf("%s: Key: %s\n", __FUNCTION__, psk.key);
	} else if (emode == ENC_TKIPAES) {
		wsec = 6;
		psk.key_len = strlen(key);
		psk.flags = WSEC_PASSPHRASE;
		memcpy(psk.key, key, strlen(key));
		printf("%s: Encryption: TKIP/AES\n", __FUNCTION__);
		printf("%s: Key: %s\n", __FUNCTION__, psk.key);
	}

	wl_ext_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);

	if (wsec == 1) {
		wl_ext_iovar_setbuf_bsscfg(dev, "wsec_key", &wsec_key, sizeof(wsec_key),
			iovar_buf, WLC_IOCTL_MAXLEN, bssidx, NULL);
	} else if (emode == ENC_TKIP || emode == ENC_AES || emode == ENC_TKIPAES) {
		if (dev) {
			wl_ext_ioctl(dev, WLC_SET_WSEC_PMK, &psk, sizeof(psk), 1);
		} else {
			ANDROID_ERROR(("%s: apdev is null\n", __FUNCTION__));
		}
	}

	return 0;
}

/*
terence 20161127:
dhd_priv sta_init
dhd_priv sta_start ssid [xxx] bssid [xx:xx:xx:xx:xx:xx]
		 amode [open|shared|wpapsk|wpa2psk|wpawpa2psk] emode [none|wep|tkip|aes|tkipaes]
		 key [xxxxx]
dhd_priv sta_stop

dhd_priv ap_init
dhd_priv ap_start ssid [xxx] hidden [y|n] chan [x]
		 amode [open|shared|wpapsk|wpa2psk|wpawpa2psk] emode [none|wep|tkip|aes|tkipaes]
		 key [xxxxx]
dhd_priv ap_stop

dhd_priv apsta_init ifname [wlan1]
dhd_priv apsta_start ssid [xxx] hidden [y|n] chan [x]
		 amode [open|shared|wpapsk|wpa2psk|wpawpa2psk] emode [none|wep|tkip|aes|tkipaes]
		 key [xxxxx]
dhd_priv apsta_stop
*/

static int
wl_ext_apsta_init(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	s32 val = 0;
	char *pch, *pick_tmp, *param;
	wlc_ssid_t ssid = { 0, {0} };
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct wl_apsta_params *apsta_params = &g_apsta_params;

	if (apsta_params->action >= ACTION_INIT) {
		ANDROID_ERROR(("%s: don't init twice\n", __FUNCTION__));
		return -1;
	}

	memset(apsta_params, 0, sizeof(struct wl_apsta_params));

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "sta_init")) {
			apsta_params->apstamode = ISTA_MODE;
		} else if (!strcmp(param, "ap_init")) {
			apsta_params->apstamode = IAP_MODE;
		} else if (!strcmp(param, "apstta_init")) {
			apsta_params->apstamode = IAPSTA_MODE;
		} else if (!strcmp(param, "ifname")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(apsta_params->apsta_if.ifname, pch);
			else {
				ANDROID_ERROR(("%s: ifname [wlan1]\n", __FUNCTION__));
				return -1;
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	strcpy(apsta_params->ssid, "ttt");
	apsta_params->channel = 1;

	if (apsta_params->apstamode == ISTA_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "apsta", 0);
		val = 0;
		wl_ext_ioctl(dev, WLC_SET_AP, &val, sizeof(val), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apsta_params->apstamode == IAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setint(dev, "apsta", 0);
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_AP, &val, sizeof(val), 1);
	} else if (apsta_params->apstamode == IAPSTA_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		val = 0;
		wl_ext_ioctl(dev, WLC_SET_PM, &val, sizeof(val), 1);
		wl_ext_iovar_setbuf_bsscfg(dev, "ssid", &ssid, sizeof(ssid), iovar_buf,
			WLC_IOCTL_SMLEN, 1, NULL);
	}

	apsta_params->action = ACTION_INIT;
	ret = wl_ext_get_ioctl_ver(dev, &apsta_params->ioctl_ver);

	return ret;
}

static int
wl_ext_apsta_stop(struct net_device *dev)
{
	int ret = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct wl_apsta_params *apsta_params = &g_apsta_params;

	if (apsta_params->action < ACTION_INIT) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	} else
		apsta_params->action = ACTION_DISABLE;

	printf("%s: apstamode=%d\n", __FUNCTION__, apsta_params->apstamode);

	if (apsta_params->apstamode == ISTA_MODE) {
		wl_ext_ioctl(dev, WLC_DISASSOC, NULL, 0, 1);
	} else if (apsta_params->apstamode == IAP_MODE) {
		wl_ext_ioctl(dev, WLC_DOWN, NULL, 0, 1);
		wl_ext_iovar_setint(dev, "mpc", 1);
	} else if (apsta_params->apstamode == IAPSTA_MODE) {
		bss_setbuf.cfg = htod32(apsta_params->apsta_if.bssidx);
		bss_setbuf.val = htod32(0);
		wl_ext_iovar_setbuf(dev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
		wl_ext_iovar_setint(dev, "mpc", 1);
	}
	return ret;
}

static int
wl_ext_bringup_apsta(struct net_device *dev, struct wl_apsta_params *apsta_params)
{
	int ret = 0;
	s32 val = 0;
	s8 iovar_buf[WLC_IOCTL_SMLEN];
	wlc_ssid_t ssid = { 0, {0} };
	s32 bssidx = 0;
	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;
	struct net_device *apdev;
	apstamode_t apstamode = apsta_params->apstamode;
	int hidden = apsta_params->hidden;

	printf("%s: apstamode=%d\n", __FUNCTION__, apstamode);

	apdev = dev;
	if (apstamode == ISTA_MODE || apstamode == IAP_MODE) {
		bssidx = 0;
	} else if (apstamode == IAPSTA_MODE) {
		apdev = apsta_params->apsta_if.dev;
		bssidx = apsta_params->apsta_if.bssidx;
	}
	ssid.SSID_len = strlen(apsta_params->ssid);
	memcpy(ssid.SSID, apsta_params->ssid, ssid.SSID_len);

	ret = wl_ext_apsta_stop(dev);
	if (ret)
		goto exit;

	if (apsta_params->action < ACTION_INIT) {
		ANDROID_ERROR(("%s: please init first\n", __FUNCTION__));
		return -1;
	} else
		apsta_params->action = ACTION_ENABLE;

	if (apstamode == ISTA_MODE) {
		wl_ext_ioctl(dev, WLC_DISASSOC, NULL, 0, 1);
	} else if (apstamode == IAP_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == IAPSTA_MODE) {
		wl_ext_iovar_setint(dev, "mpc", 0);
		wl_ext_iovar_setbuf_bsscfg(dev, "ssid", &ssid, sizeof(ssid),
			iovar_buf, WLC_IOCTL_SMLEN, bssidx, NULL);
	}

	if (apstamode == IAP_MODE || apstamode == IAPSTA_MODE) {
		wl_ext_set_bgnmode(apdev, apsta_params);
		wl_ext_set_chanspec(apdev, apsta_params->channel);
	}
	wl_ext_set_amode(apdev, apsta_params, bssidx);
	wl_ext_set_emode(apdev, apsta_params, bssidx);

	if (apstamode == ISTA_MODE) {
		if (!ETHER_ISBCAST(&apsta_params->bssid) && !ETHER_ISNULLADDR(&apsta_params->bssid)) {
			printf("%s: BSSID: %pM\n", __FUNCTION__, &apsta_params->bssid);
			wl_ext_ioctl(dev, WLC_SET_BSSID, &apsta_params->bssid, ETHER_ADDR_LEN, 1);
		}
		val = 1;
		wl_ext_ioctl(dev, WLC_SET_INFRA, &val, sizeof(val), 1);
	} else if (apstamode == IAP_MODE || apstamode == IAPSTA_MODE) {
		wl_ext_iovar_setint_bsscfg(apdev, "maxassoc", 3, bssidx);
		printf("%s: Broadcast SSID: %s\n", __FUNCTION__, apsta_params->hidden ? "OFF":"ON");
		if (apsta_params->hidden) {
			wl_ext_ioctl(apdev, WLC_SET_CLOSED, &hidden, sizeof(hidden), 1);
		} else {
			wl_ext_ioctl(apdev, WLC_SET_CLOSED, &hidden, sizeof(hidden), 0);
		}
	}

	if (apstamode == ISTA_MODE) {
		wl_ext_ioctl(dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
	} else if (apstamode == IAP_MODE) {
		wl_ext_ioctl(dev, WLC_SET_SSID, &ssid, sizeof(ssid), 1);
		wl_ext_ioctl(dev, WLC_UP, NULL, 0, 1);
	} else if (apstamode == IAPSTA_MODE) {
		bss_setbuf.cfg = htod32(apsta_params->apsta_if.bssidx);
		bss_setbuf.val = htod32(1);
		wl_ext_iovar_setbuf(apdev, "bss", &bss_setbuf, sizeof(bss_setbuf),
			iovar_buf, WLC_IOCTL_SMLEN, NULL);
	}
	printf("%s: SSID: %s\n", __FUNCTION__, apsta_params->ssid);

exit:
	return ret;
}

static int
wl_ext_apsta_start(struct net_device *dev, char *command, int total_len)
{
	int ret = 0, i;
	char *pch, *pick_tmp, *param;
	struct wl_apsta_params *apsta_params = &g_apsta_params;

	printf("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len);

	pick_tmp = command;
	param = bcmstrtok(&pick_tmp, " ", 0); // skip xxx_init
	param = bcmstrtok(&pick_tmp, " ", 0);
	while (param != NULL) {
		if (!strcmp(param, "ssid")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				strcpy(apsta_params->ssid, pch);
		} else if (!strcmp(param, "bssid")) {
			pch = bcmstrtok(&pick_tmp, ": ", 0);
			for (i=0; i<6 && pch; i++) {
				((u8 *)&apsta_params->bssid)[i] = (int)simple_strtol(pch, NULL, 16);
				pch = bcmstrtok(&pick_tmp, ": ", 0);
			}
		} else if (!strcmp(param, "bgnmode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "b"))
					apsta_params->bgnmode = IEEE80211B;
				else if (!strcmp(pch, "g"))
					apsta_params->bgnmode = IEEE80211G;
				else if (!strcmp(pch, "bg"))
					apsta_params->bgnmode = IEEE80211BG;
				else if (!strcmp(pch, "bgn"))
					apsta_params->bgnmode = IEEE80211BGN;
				else if (!strcmp(pch, "bgnac"))
					apsta_params->bgnmode = IEEE80211BGNAC;
				else {
					ANDROID_ERROR(("%s: bgnmode [b|g|bg|bgn|bgnac]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "hidden")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "n"))
					apsta_params->hidden = 0;
				else if (!strcmp(pch, "y"))
					apsta_params->hidden = 1;
				else {
					ANDROID_ERROR(("%s: hidden [y|n]\n", __FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "chan")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch)
				apsta_params->channel = (int)simple_strtol(pch, NULL, 10);
		} else if (!strcmp(param, "amode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "open"))
					apsta_params->amode = AUTH_OPEN;
				else if (!strcmp(pch, "shared"))
					apsta_params->amode = AUTH_SHARED;
				else if (!strcmp(pch, "wpapsk"))
					apsta_params->amode = AUTH_WPAPSK;
				else if (!strcmp(pch, "wpa2psk"))
					apsta_params->amode = AUTH_WPA2PSK;
				else if (!strcmp(pch, "wpawpa2psk"))
					apsta_params->amode = AUTH_WPAWPA2PSK;
				else {
					ANDROID_ERROR(("%s: amode [open|shared|wpapsk|wpa2psk|wpawpa2psk]\n",
						__FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "emode")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				if (!strcmp(pch, "none"))
					apsta_params->emode = ENC_NONE;
				else if (!strcmp(pch, "wep"))
					apsta_params->emode = ENC_WEP;
				else if (!strcmp(pch, "tkip"))
					apsta_params->emode = ENC_TKIP;
				else if (!strcmp(pch, "aes"))
					apsta_params->emode = ENC_AES;
				else if (!strcmp(pch, "tkipaes"))
					apsta_params->emode = ENC_TKIPAES;
				else {
					ANDROID_ERROR(("%s: emode [none|wep|tkip|aes|tkipaes]\n",
						__FUNCTION__));
					return -1;
				}
			}
		} else if (!strcmp(param, "key")) {
			pch = bcmstrtok(&pick_tmp, " ", 0);
			if (pch) {
				strcpy(apsta_params->key, pch);
			}
		}
		param = bcmstrtok(&pick_tmp, " ", 0);
	}

	ret = wl_ext_bringup_apsta(dev, apsta_params);

	return ret;
}

int wl_android_ext_attach_netdev(struct net_device *net, uint8 bssidx)
{
	g_apsta_params.apsta_if.dev = net;
	g_apsta_params.apsta_if.bssidx = bssidx;
	if (strlen(g_apsta_params.apsta_if.ifname)) {
		memset(net->name, 0, sizeof(IFNAMSIZ));
		strcpy(net->name, g_apsta_params.apsta_if.ifname);
		net->name[IFNAMSIZ - 1] = '\0';
	}
	printf("%s: apsta_params.dev=%p, bssidx=%d, ifname=%s\n",
		__FUNCTION__, net, bssidx, net->name);

	return 0;
}

int wl_android_ext_dettach_netdev(void)
{
	struct wl_apsta_params *apsta_params = &g_apsta_params;

	printf("%s: Enter\n", __FUNCTION__);
	memset(apsta_params, 0, sizeof(struct wl_apsta_params));

	return 0;
}
#endif

int wl_android_ext_priv_cmd(struct net_device *net, char *command, int total_len,
		int *bytes_written)
{
	int ret = 0;

	if(strnicmp(command, CMD_GET_CHANNEL, strlen(CMD_GET_CHANNEL)) == 0) {
		*bytes_written = wl_ext_get_channel(net, command, total_len);
	}
	else if (strnicmp(command, CMD_SET_CHANNEL, strlen(CMD_SET_CHANNEL)) == 0) {
		*bytes_written = wl_ext_set_channel(net, command, total_len);
	}
	else if (strnicmp(command, CMD_SET_ROAM, strlen(CMD_SET_ROAM)) == 0) {
		*bytes_written = wl_ext_set_roam_trigger(net, command, total_len);
	}
	else if (strnicmp(command, CMD_GET_ROAM, strlen(CMD_GET_ROAM)) == 0) {
		*bytes_written = wl_ext_get_roam_trigger(net, command, total_len);
	}
	else if (strnicmp(command, CMD_GET_KEEP_ALIVE, strlen(CMD_GET_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_GET_KEEP_ALIVE) + 1;
		*bytes_written = wl_ext_get_keep_alive(net, command+skip, total_len-skip);
	}
	else if (strnicmp(command, CMD_GET_PM, strlen(CMD_GET_PM)) == 0) {
		*bytes_written = wl_ext_get_pm(net, command, total_len);
	}
	else if (strnicmp(command, CMD_SET_PM, strlen(CMD_SET_PM)) == 0) {
		*bytes_written = wl_ext_set_pm(net, command, total_len);
	}
	else if (strnicmp(command, CMD_MONITOR, strlen(CMD_MONITOR)) == 0) {
		*bytes_written = wl_ext_set_monitor(net, command, total_len);
	}
#ifdef WL_EXT_IAPSTA
	else if (strnicmp(command, CMD_STA_INIT, strlen(CMD_STA_INIT)) == 0 ||
			strnicmp(command, CMD_AP_INIT, strlen(CMD_AP_INIT)) == 0 ||
			strnicmp(command, CMD_APSTA_INIT, strlen(CMD_APSTA_INIT)) == 0) {
		*bytes_written = wl_ext_apsta_init(net, command, total_len);
	}
	else if (strnicmp(command, CMD_STA_START, strlen(CMD_STA_START)) == 0 ||
			strnicmp(command, CMD_AP_START, strlen(CMD_AP_START)) == 0 ||
			strnicmp(command, CMD_APSTA_START, strlen(CMD_APSTA_START)) == 0) {
		*bytes_written = wl_ext_apsta_start(net, command, total_len);
	}
	else if (strnicmp(command, CMD_STA_STOP, strlen(CMD_STA_STOP)) == 0 ||
			strnicmp(command, CMD_AP_STOP, strlen(CMD_AP_STOP)) == 0 ||
			strnicmp(command, CMD_APSTA_STOP, strlen(CMD_APSTA_STOP)) == 0) {
		*bytes_written = wl_ext_apsta_stop(net);
	}
#endif
	else
		ret = -1;

	return ret;
}

#if defined(RSSIAVG)
void
wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *cur, **rssi_head;
	int i=0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;

	for (;node;) {
		ANDROID_INFO(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->BSSID));
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*rssi_head = NULL;
}

void
wl_delete_dirty_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;
	struct timeval now;

	do_gettimeofday(&now);

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, u8 *bssid)
{
	wl_rssi_cache_t *node, *prev, **rssi_head;
	int i = -1, tmp = 0;

	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *rssi_head) {
				tmp = 1;
				*rssi_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_INFO(("%s: Del %d with BSSID %pM\n",
				__FUNCTION__, i, &node->BSSID));
			kfree(node);
			if (tmp == 1) {
				node = *rssi_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl)
{
	wl_rssi_cache_t *node, **rssi_head;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *rssi_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

int
wl_update_connected_rssi_cache(struct net_device *net, wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	int j, k=0;
	int rssi, error=0;
	struct ether_addr bssid;
	struct timeval now, timeout;

	if (!g_wifi_on)
		return 0;

	error = wldev_ioctl(net, WLC_GET_BSSID, &bssid, sizeof(bssid), false);
	if (error == BCME_NOTASSOCIATED) {
		ANDROID_INFO(("%s: Not Associated! res:%d\n", __FUNCTION__, error));
		return 0;
	}
	if (error) {
		ANDROID_ERROR(("Could not get bssid (%d)\n", error));
	}
	error = wldev_get_rssi(net, &rssi);
	if (error) {
		ANDROID_ERROR(("Could not get rssi (%d)\n", error));
		return error;
	}

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	/* update RSSI */
	rssi_head = &rssi_cache_ctrl->m_cache_head;
	node = *rssi_head;
	prev = NULL;
	for (;node;) {
		if (!memcmp(&node->BSSID, &bssid, ETHER_ADDR_LEN)) {
			ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%d\n",
				__FUNCTION__, k, &bssid, rssi));
			for (j=0; j<RSSIAVG_LEN-1; j++)
				node->RSSI[j] = node->RSSI[j+1];
			node->RSSI[j] = rssi;
			node->dirty = 0;
			node->tv = timeout;
			goto exit;
		}
		prev = node;
		node = node->next;
		k++;
	}

	leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
	if (!leaf) {
		ANDROID_ERROR(("%s: Memory alloc failure %d\n",
			__FUNCTION__, (int)sizeof(wl_rssi_cache_t)));
		return 0;
	}
	ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%3d in the leaf\n",
			__FUNCTION__, k, &bssid, rssi));

	leaf->next = NULL;
	leaf->dirty = 0;
	leaf->tv = timeout;
	memcpy(&leaf->BSSID, &bssid, ETHER_ADDR_LEN);
	for (j=0; j<RSSIAVG_LEN; j++)
		leaf->RSSI[j] = rssi;

	if (!prev)
		*rssi_head = leaf;
	else
		prev->next = leaf;

exit:
	*rssi_avg = (int)wl_get_avg_rssi(rssi_cache_ctrl, &bssid);

	return error;
}

void
wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, wl_scan_results_t *ss_list)
{
	wl_rssi_cache_t *node, *prev, *leaf, **rssi_head;
	wl_bss_info_t *bi = NULL;
	int i, j, k;
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + RSSICACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, RSSICACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	/* update RSSI */
	for (i = 0; i < ss_list->count; i++) {
		node = *rssi_head;
		prev = NULL;
		k = 0;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;
		for (;node;) {
			if (!memcmp(&node->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				ANDROID_INFO(("%s: Update %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
					__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));
				for (j=0; j<RSSIAVG_LEN-1; j++)
					node->RSSI[j] = node->RSSI[j+1];
				node->RSSI[j] = dtoh16(bi->RSSI);
				node->dirty = 0;
				node->tv = timeout;
				break;
			}
			prev = node;
			node = node->next;
			k++;
		}

		if (node)
			continue;

		leaf = kmalloc(sizeof(wl_rssi_cache_t), GFP_KERNEL);
		if (!leaf) {
			ANDROID_ERROR(("%s: Memory alloc failure %d\n",
				__FUNCTION__, (int)sizeof(wl_rssi_cache_t)));
			return;
		}
		ANDROID_INFO(("%s: Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\" in the leaf\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		memcpy(&leaf->BSSID, &bi->BSSID, ETHER_ADDR_LEN);
		for (j=0; j<RSSIAVG_LEN; j++)
			leaf->RSSI[j] = dtoh16(bi->RSSI);

		if (!prev)
			*rssi_head = leaf;
		else
			prev->next = leaf;
	}
}

int16
wl_get_avg_rssi(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, void *addr)
{
	wl_rssi_cache_t *node, **rssi_head;
	int j, rssi_sum, rssi=RSSI_MINVAL;

	rssi_head = &rssi_cache_ctrl->m_cache_head;

	node = *rssi_head;
	for (;node;) {
		if (!memcmp(&node->BSSID, addr, ETHER_ADDR_LEN)) {
			rssi_sum = 0;
			rssi = 0;
			for (j=0; j<RSSIAVG_LEN; j++)
				rssi_sum += node->RSSI[RSSIAVG_LEN-j-1];
			rssi = rssi_sum / j;
			break;
		}
		node = node->next;
	}
	rssi = MIN(rssi, RSSI_MAXVAL);
	if (rssi == RSSI_MINVAL) {
		ANDROID_ERROR(("%s: BSSID %pM does not in RSSI cache\n",
		__FUNCTION__, addr));
	}
	return (int16)rssi;
}
#endif

#if defined(RSSIOFFSET)
int
wl_update_rssi_offset(struct net_device *net, int rssi)
{
#if defined(RSSIOFFSET_NEW)
	int j;
#endif

	if (!g_wifi_on)
		return rssi;

#if defined(RSSIOFFSET_NEW)
	for (j=0; j<RSSI_OFFSET; j++) {
		if (rssi - (RSSI_OFFSET_MINVAL+RSSI_OFFSET_INTVAL*(j+1)) < 0)
			break;
	}
	rssi += j;
#else
	rssi += RSSI_OFFSET;
#endif
	return MIN(rssi, RSSI_MAXVAL);
}
#endif

#if defined(BSSCACHE)
void
wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *cur, **bss_head;
	int i=0;

	ANDROID_TRACE(("%s called\n", __FUNCTION__));

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;

	for (;node;) {
		ANDROID_TRACE(("%s: Free %d with BSSID %pM\n",
			__FUNCTION__, i, &node->results.bss_info->BSSID));
		cur = node;
		node = cur->next;
		kfree(cur);
		i++;
	}
	*bss_head = NULL;
}

void
wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;
	struct timeval now;

	do_gettimeofday(&now);

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (now.tv_sec > node->tv.tv_sec) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid)
{
	wl_bss_cache_t *node, *prev, **bss_head;
	int i = -1, tmp = 0;

	bss_head = &bss_cache_ctrl->m_cache_head;
	node = *bss_head;
	prev = node;
	for (;node;) {
		i++;
		if (!memcmp(&node->results.bss_info->BSSID, bssid, ETHER_ADDR_LEN)) {
			if (node == *bss_head) {
				tmp = 1;
				*bss_head = node->next;
			} else {
				tmp = 0;
				prev->next = node->next;
			}
			ANDROID_TRACE(("%s: Del %d with BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, i, &node->results.bss_info->BSSID,
				dtoh16(node->results.bss_info->RSSI), node->results.bss_info->SSID));
			kfree(node);
			if (tmp == 1) {
				node = *bss_head;
				prev = node;
			} else {
				node = prev->next;
			}
			continue;
		}
		prev = node;
		node = node->next;
	}
}

void
wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	wl_bss_cache_t *node, **bss_head;

	bss_head = &bss_cache_ctrl->m_cache_head;

	/* reset dirty */
	node = *bss_head;
	for (;node;) {
		node->dirty += 1;
		node = node->next;
	}
}

void dump_bss_cache(
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif
	wl_bss_cache_t *node)
{
	int k = 0;
	int16 rssi;

	for (;node;) {
#if defined(RSSIAVG)
		rssi = wl_get_avg_rssi(rssi_cache_ctrl, &node->results.bss_info->BSSID);
#else
		rssi = dtoh16(node->results.bss_info->RSSI);
#endif
		ANDROID_TRACE(("%s: dump %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
			__FUNCTION__, k, &node->results.bss_info->BSSID, rssi, node->results.bss_info->SSID));
		k++;
		node = node->next;
	}
}

void
wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif
	wl_scan_results_t *ss_list)
{
	wl_bss_cache_t *node, *prev, *leaf, **bss_head;
	wl_bss_info_t *bi = NULL;
	int i, k=0;
#if defined(SORT_BSS_BY_RSSI)
	int16 rssi, rssi_node;
#endif
	struct timeval now, timeout;

	if (!ss_list->count)
		return;

	do_gettimeofday(&now);
	timeout.tv_sec = now.tv_sec + BSSCACHE_TIMEOUT;
	if (timeout.tv_sec < now.tv_sec) {
		/*
		 * Integer overflow - assume long enough timeout to be assumed
		 * to be infinite, i.e., the timeout would never happen.
		 */
		ANDROID_TRACE(("%s: Too long timeout (secs=%d) to ever happen - now=%lu, timeout=%lu",
			__FUNCTION__, BSSCACHE_TIMEOUT, now.tv_sec, timeout.tv_sec));
	}

	bss_head = &bss_cache_ctrl->m_cache_head;

	for (i=0; i < ss_list->count; i++) {
		node = *bss_head;
		prev = NULL;
		bi = bi ? (wl_bss_info_t *)((uintptr)bi + dtoh32(bi->length)) : ss_list->bss_info;

		for (;node;) {
			if (!memcmp(&node->results.bss_info->BSSID, &bi->BSSID, ETHER_ADDR_LEN)) {
				if (node == *bss_head)
					*bss_head = node->next;
				else {
					prev->next = node->next;
				}
				break;
			}
			prev = node;
			node = node->next;
		}

		leaf = kmalloc(dtoh32(bi->length) + sizeof(wl_bss_cache_t), GFP_KERNEL);
		if (!leaf) {
			ANDROID_ERROR(("%s: Memory alloc failure %d\n", __FUNCTION__,
				dtoh32(bi->length) + (int)sizeof(wl_bss_cache_t)));
			return;
		}
		if (node) {
			kfree(node);
			node = NULL;
			ANDROID_TRACE(("%s: Update %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));
		} else
			ANDROID_TRACE(("%s: Add %d with cached BSSID %pM, RSSI=%3d, SSID \"%s\"\n",
				__FUNCTION__, k, &bi->BSSID, dtoh16(bi->RSSI), bi->SSID));

		memcpy(leaf->results.bss_info, bi, dtoh32(bi->length));
		leaf->next = NULL;
		leaf->dirty = 0;
		leaf->tv = timeout;
		leaf->results.count = 1;
		leaf->results.version = ss_list->version;
		k++;

		if (*bss_head == NULL)
			*bss_head = leaf;
		else {
#if defined(SORT_BSS_BY_RSSI)
			node = *bss_head;
#if defined(RSSIAVG)
			rssi = wl_get_avg_rssi(rssi_cache_ctrl, &leaf->results.bss_info->BSSID);
#else
			rssi = dtoh16(leaf->results.bss_info->RSSI);
#endif
			for (;node;) {
#if defined(RSSIAVG)
				rssi_node = wl_get_avg_rssi(rssi_cache_ctrl, &node->results.bss_info->BSSID);
#else
				rssi_node = dtoh16(node->results.bss_info->RSSI);
#endif
				if (rssi > rssi_node) {
					leaf->next = node;
					if (node == *bss_head)
						*bss_head = leaf;
					else
						prev->next = leaf;
					break;
				}
				prev = node;
				node = node->next;
			}
			if (node == NULL)
				prev->next = leaf;
#else
			leaf->next = *bss_head;
			*bss_head = leaf;
#endif
		}
	}
	dump_bss_cache(
#if defined(RSSIAVG)
		rssi_cache_ctrl,
#endif
		*bss_head);
}

void
wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl)
{
	ANDROID_TRACE(("%s:\n", __FUNCTION__));
	wl_free_bss_cache(bss_cache_ctrl);
}
#endif
