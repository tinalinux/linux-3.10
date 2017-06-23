/*
 * sound\soc\sunxi\sunxi-sndhub.c
 * (C) Copyright 2014-2018
 * Allwinnertech Technology Co., Ltd. <www.allwinnertech.com>
 * Wolfgang huang <huangjinhui@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/io.h>
#include <linux/of.h>

static const struct snd_kcontrol_new sunxi_ahub_card_controls[] = {
	SOC_DAPM_PIN_SWITCH("I2S0IN"),
	SOC_DAPM_PIN_SWITCH("I2S0OUT"),
	SOC_DAPM_PIN_SWITCH("I2S1IN"),
	SOC_DAPM_PIN_SWITCH("I2S1OUT"),
	SOC_DAPM_PIN_SWITCH("I2S2IN"),
	SOC_DAPM_PIN_SWITCH("I2S2OUT"),
	SOC_DAPM_PIN_SWITCH("I2S3IN"),
	SOC_DAPM_PIN_SWITCH("I2S3OUT"),
};

/* the input & output dir depends on view of audio hub */
static const struct snd_soc_dapm_widget sunxi_ahub_card_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("I2S0IN", NULL),
	SND_SOC_DAPM_LINE("I2S0OUT", NULL),
	SND_SOC_DAPM_LINE("I2S1IN", NULL),
	SND_SOC_DAPM_LINE("I2S1OUT", NULL),
	SND_SOC_DAPM_LINE("I2S2IN", NULL),
	SND_SOC_DAPM_LINE("I2S2OUT", NULL),
	SND_SOC_DAPM_LINE("I2S3IN", NULL),
	SND_SOC_DAPM_LINE("I2S3OUT", NULL),
};

/* the input & output dir depends on view of audio hub */
static const struct snd_soc_dapm_route sunxi_ahub_card_routes[] = {
	{"I2S0 DAC", NULL, "I2S0IN"},
	{"I2S1 DAC", NULL, "I2S1IN"},
	{"I2S2 DAC", NULL, "I2S2IN"},
	{"I2S3 DAC", NULL, "I2S3IN"},
	{"I2S0OUT", NULL, "I2S0 ADC"},
	{"I2S1OUT", NULL, "I2S1 ADC"},
	{"I2S2OUT", NULL, "I2S2 ADC"},
	{"I2S3OUT", NULL, "I2S3 ADC"},
};

static int sunxi_ahub_card_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	snd_soc_dapm_disable_pin(&codec->dapm, "I2S0IN");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S0OUT");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S1IN");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S1OUT");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S2IN");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S2OUT");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S3IN");
	snd_soc_dapm_disable_pin(&codec->dapm, "I2S3OUT");

	snd_soc_dapm_sync(&codec->dapm);
	return 0;
}

static int sunxi_sndahub_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	unsigned int freq;
	int ret;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 64000:
	case 128000:
	case 12000:
	case 24000:
	case 48000:
	case 96000:
	case 192000:
		freq = 24576000;
		break;
	case	11025:
	case	22050:
	case	44100:
	case	88200:
	case	176400:
		freq = 22579200;
		break;
	default:
		dev_err(card->dev, "unsupport freq\n");
		return -EINVAL;
	}

	/*set system clock source freq and set the mode as i2s0 or pcm*/
	ret = snd_soc_dai_set_sysclk(codec_dai, 0 , freq, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops sunxi_sndahub_ops = {
	.hw_params	= sunxi_sndahub_hw_params,
};

static struct snd_soc_dai_link sunxi_sndahub_dai_link[] = {
	{
		.name = "Primary",
		.stream_name = "Media Stream",
		.codec_dai_name = "sunxi-ahub-aif1",
		.init = sunxi_ahub_card_init,
		.ops = &sunxi_sndahub_ops,
	},
	{
		.name = "Sec",
		.stream_name = "System Stream",
		.codec_dai_name = "sunxi-ahub-aif2",
		.ops = &sunxi_sndahub_ops,
	},
	{
		.name = "Thr",
		.stream_name = "Accompany Stream",
		.codec_dai_name = "sunxi-ahub-aif3",
		.ops = &sunxi_sndahub_ops,
	},
};

static struct snd_soc_card snd_soc_sunxi_sndahub = {
	.name			= "sndahub",
	.owner			= THIS_MODULE,
	.controls		= sunxi_ahub_card_controls,
	.num_controls		= ARRAY_SIZE(sunxi_ahub_card_controls),
	.dapm_widgets		= sunxi_ahub_card_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sunxi_ahub_card_dapm_widgets),
	.dapm_routes		= sunxi_ahub_card_routes,
	.num_dapm_routes	= ARRAY_SIZE(sunxi_ahub_card_routes),
	.dai_link		= sunxi_sndahub_dai_link,
	.num_links		= ARRAY_SIZE(sunxi_sndahub_dai_link),
};

static int sunxi_sndahub_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &snd_soc_sunxi_sndahub;
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int i;

	card->dev = &pdev->dev;

	sunxi_sndahub_dai_link[0].cpu_dai_name = NULL;
	sunxi_sndahub_dai_link[0].cpu_of_node = of_parse_phandle(np,
					"sunxi,cpudai-controller0", 0);
	if (!sunxi_sndahub_dai_link[0].cpu_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,cpudai-controller0' missing or invalid\n");
		return -EINVAL;
	} else {
		sunxi_sndahub_dai_link[0].platform_name = NULL;
		sunxi_sndahub_dai_link[0].platform_of_node =
				sunxi_sndahub_dai_link[0].cpu_of_node;
	}

	sunxi_sndahub_dai_link[1].cpu_dai_name = NULL;
	sunxi_sndahub_dai_link[1].cpu_of_node = of_parse_phandle(np,
					"sunxi,cpudai-controller1", 0);
	if (!sunxi_sndahub_dai_link[1].cpu_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,cpudai-controller1' missing or invalid\n");
		return -EINVAL;
	} else {
		sunxi_sndahub_dai_link[1].platform_name = NULL;
		sunxi_sndahub_dai_link[1].platform_of_node =
				sunxi_sndahub_dai_link[1].cpu_of_node;
	}

	sunxi_sndahub_dai_link[2].cpu_dai_name = NULL;
	sunxi_sndahub_dai_link[2].cpu_of_node = of_parse_phandle(np,
					"sunxi,cpudai-controller2", 0);
	if (!sunxi_sndahub_dai_link[2].cpu_of_node) {
		dev_err(&pdev->dev, "Property 'sunxi,cpudai-controller0' missing or invalid\n");
		return -EINVAL;
	} else {
		sunxi_sndahub_dai_link[2].platform_name = NULL;
		sunxi_sndahub_dai_link[2].platform_of_node =
				sunxi_sndahub_dai_link[2].cpu_of_node;
	}

	for (i = 0; i < ARRAY_SIZE(sunxi_sndahub_dai_link); i++) {
		sunxi_sndahub_dai_link[i].codec_name = NULL;
		sunxi_sndahub_dai_link[i].codec_of_node = of_parse_phandle(np,
							"sunxi,audio-codec", 0);
	}

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	}
	return ret;
}

static int __exit sunxi_sndahub_dev_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static const struct of_device_id sunxi_ahub_of_match[] = {
	{ .compatible = "allwinner,sunxi-ahub-machine", },
	{},
};

static struct platform_driver sunxi_ahubaudio_driver = {
	.driver = {
		.name = "sndahub",
		.owner = THIS_MODULE,
		.of_match_table = sunxi_ahub_of_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = sunxi_sndahub_dev_probe,
	.remove = __exit_p(sunxi_sndahub_dev_remove),
};

module_platform_driver(sunxi_ahubaudio_driver);

MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_DESCRIPTION("SUNXI Audio Hub ASoC Machine driver");
MODULE_LICENSE("GPL");
