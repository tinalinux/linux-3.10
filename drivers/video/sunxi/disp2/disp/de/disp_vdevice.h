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
#ifndef __DISP_VDEVICE_H__
#define __DISP_VDEVICE_H__

#include "disp_private.h"
#include "disp_display.h"

extern disp_dev_t gdisp;
struct disp_device* disp_vdevice_register(struct disp_vdevice_init_data *data);
s32 disp_vdevice_unregister(struct disp_device *vdevice);
s32 disp_vdevice_get_source_ops(struct disp_vdevice_source_ops *ops);

#endif
