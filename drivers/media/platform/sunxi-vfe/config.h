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
/*
 * header file
 * set/get config from sysconfig and ini/bin file
 * Author: raymonxiu
 * 
 */
 
#ifndef __CONFIG__H__
#define __CONFIG__H__

#include "vfe.h"
#include "utility/cfg_op.h"

int fetch_config(struct vfe_dev *dev);
int read_ini_info(struct vfe_dev *dev,int isp_id, char *main_path);

#endif //__CONFIG__H__