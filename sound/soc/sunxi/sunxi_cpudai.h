/*
 *
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef __SUNXI_I2S_H__
#define __SUNXI_I2S_H__
#include "sunxi_dma.h"
#include "sunxi_rw_func.h"
#if defined(CONFIG_ARCH_SUN8IW10)
#include "sun8iw10_codec.h"
#elif defined(CONFIG_ARCH_SUN8IW11)
#include "sun8iw11_codec.h"
#endif

struct sunxi_cpudai {
	struct sunxi_dma_params play_dma_param;
	struct sunxi_dma_params capture_dma_param;
	struct snd_soc_dai_driver dai;
};
#endif