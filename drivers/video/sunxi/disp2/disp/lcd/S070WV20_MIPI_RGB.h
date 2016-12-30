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
#ifndef  __S070WV20_MIPI_RGB_PANEL_H__
#define  __S070WV20_MIPI_RGB_PANEL_H__

#include "panels.h"

#define icn_en(val) sunxi_lcd_gpio_set_value(sel, 0, val)
#define power_en(val)  sunxi_lcd_gpio_set_value(sel, 1, val)

extern __lcd_panel_t S070WV20_MIPI_RGB_panel;

#endif
