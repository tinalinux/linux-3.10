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
#ifndef __DISP_HDMI_H__
#define __DISP_HDMI_H__

#include "disp_private.h"

s32 disp_init_hdmi(disp_bsp_init_para * para);

struct disp_device* disp_get_hdmi(u32 disp);

#endif
