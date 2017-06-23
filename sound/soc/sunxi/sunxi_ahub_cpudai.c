/*
 * sound\soc\sunxi\sunxi_ahub_cpudai.c
 * (C) Copyright 2014-2018
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/dma/sunxi-dma.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "sunxi_dma.h"
#include "sunxi_ahub.h"

#define DRV_NAME "sunxi-ahub-cpudai"

struct sunxi_ahub_cpudai_priv {
	unsigned int id;
	struct sunxi_dma_params playback_dma_param;
	struct sunxi_dma_params capture_dma_param;
};

static int sunxi_ahub_cpudai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct sunxi_ahub_cpudai_priv *sunxi_ahub_cpudai =
					snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_dai_set_dma_data(dai, substream,
					&sunxi_ahub_cpudai->playback_dma_param);
	else
		snd_soc_dai_set_dma_data(dai, substream,
					&sunxi_ahub_cpudai->capture_dma_param);

	return 0;
}

static struct snd_soc_dai_ops sunxi_ahub_cpudai_dai_ops = {
	.startup = sunxi_ahub_cpudai_startup,
};

static struct snd_soc_dai_driver sunxi_ahub_cpudai_dai = {
	.playback = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE
			| SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE
			| SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops	= &sunxi_ahub_cpudai_dai_ops,

};

static const struct snd_soc_component_driver sunxi_ahub_cpudai_component = {
	.name		= DRV_NAME,
};
static const struct of_device_id sunxi_ahub_cpudai_of_match[] = {
	{ .compatible = "allwinner,sunxi-ahub-cpudai", },
	{},
};

static int  sunxi_ahub_cpudai_dev_probe(struct platform_device *pdev)
{
	struct resource res;
	struct sunxi_ahub_cpudai_priv *sunxi_ahub_cpudai;
	struct device_node *np = pdev->dev.of_node;
	unsigned int temp_val;
	int ret;

	sunxi_ahub_cpudai = devm_kzalloc(&pdev->dev,
			sizeof(struct sunxi_ahub_cpudai_priv), GFP_KERNEL);
	if (!sunxi_ahub_cpudai) {
		dev_err(&pdev->dev, "Can't allocate sunxi_cpudai\n");
		ret = -ENOMEM;
		goto err_node_put;
	}
	dev_set_drvdata(&pdev->dev, sunxi_ahub_cpudai);

	ret = of_property_read_u32(np, "id", &temp_val);
	if (ret < 0) {
		dev_err(&pdev->dev, "id configuration missing or invalid\n");
		goto err_devm_kfree;
	} else {
		sunxi_ahub_cpudai->id = temp_val;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "Can't parse device node resource\n");
		ret = -ENODEV;
		goto err_devm_kfree;
	}

	switch (sunxi_ahub_cpudai->id) {
	case 0:
		sunxi_ahub_cpudai->playback_dma_param.dma_drq_type_num =
							DRQDST_AHUB0_TX;
		sunxi_ahub_cpudai->capture_dma_param.dma_drq_type_num =
							DRQSRC_AHUB0_RX;
		break;
	case 1:
		sunxi_ahub_cpudai->playback_dma_param.dma_drq_type_num =
							DRQDST_AHUB1_TX;
		sunxi_ahub_cpudai->capture_dma_param.dma_drq_type_num =
							DRQSRC_AHUB1_RX;
		break;
	case 2:
		sunxi_ahub_cpudai->playback_dma_param.dma_drq_type_num =
							DRQDST_AHUB2_TX;
		sunxi_ahub_cpudai->capture_dma_param.dma_drq_type_num =
							DRQSRC_AHUB2_RX;
		break;
	default:
		dev_err(&pdev->dev, "ahub cpudai id invalid\n");
		ret = -EINVAL;
		goto err_devm_kfree;
	}
	sunxi_ahub_cpudai->playback_dma_param.dma_addr =
		res.start + SUNXI_AHUB_APBIF_TXFIFO(sunxi_ahub_cpudai->id);
	sunxi_ahub_cpudai->playback_dma_param.dst_maxburst = 4;
	sunxi_ahub_cpudai->playback_dma_param.src_maxburst = 4;

	sunxi_ahub_cpudai->capture_dma_param.dma_addr =
		res.start + SUNXI_AHUB_APBIF_RXFIFO(sunxi_ahub_cpudai->id);
	sunxi_ahub_cpudai->capture_dma_param.src_maxburst = 4;
	sunxi_ahub_cpudai->capture_dma_param.dst_maxburst = 4;

	ret = snd_soc_register_component(&pdev->dev,
			&sunxi_ahub_cpudai_component,
			&sunxi_ahub_cpudai_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -EBUSY;
		goto err_devm_kfree;
	}

	ret = asoc_dma_platform_register(&pdev->dev, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_component;
	}

	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_devm_kfree:
	devm_kfree(&pdev->dev, sunxi_ahub_cpudai);
err_node_put:
	of_node_put(np);
	return ret;
}

static int __exit sunxi_ahub_cpudai_dev_remove(struct platform_device *pdev)
{
	struct sunxi_ahub_cpudai_priv *sunxi_ahub_cpudai =
					dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_component(&pdev->dev);
	devm_kfree(&pdev->dev, sunxi_ahub_cpudai);

	return 0;
}

static struct platform_driver sunxi_ahub_cpudai_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_ahub_cpudai_of_match,
	},
	.probe = sunxi_ahub_cpudai_dev_probe,
	.remove = __exit_p(sunxi_ahub_cpudai_dev_remove),
};

module_platform_driver(sunxi_ahub_cpudai_driver);

MODULE_AUTHOR("wolfgang huang <huangjinhui@allwinnertech.com>");
MODULE_DESCRIPTION("SUNXI Audio Hub cpudai ASoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
