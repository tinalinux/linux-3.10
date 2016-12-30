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

#ifndef __EINK_BUFFER_MANAGER_H__
#define __EINK_BUFFER_MANAGER_H__

#include "disp_eink.h"

s32 ring_buffer_manager_init();
s32 ring_buffer_manager_exit();
bool is_ring_queue_full();
bool is_ring_queue_empty();
u8* get_current_image();
u8* get_last_image();
s32 queue_image(u32 mode,  struct area_info update_area);
s32 dequeue_image();

#endif
