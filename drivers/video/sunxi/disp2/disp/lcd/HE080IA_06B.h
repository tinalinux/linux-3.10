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
#ifndef  __DEFAULT_PANEL_H__
#define  __DEFAULT_PANEL_H__

#include "panels.h"

#define panel_rst(v)    (sunxi_lcd_gpio_set_value(0, 0, v))
extern __lcd_panel_t inet_dsi_panel;

#endif
