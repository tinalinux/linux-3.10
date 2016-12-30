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
#include "disp_features.h"

int bsp_disp_feat_get_num_screens(void)
{
	return de_feat_get_num_screens();
}

int bsp_disp_feat_get_num_devices(void)
{
	return de_feat_get_num_devices();
}

int bsp_disp_feat_get_num_channels(unsigned int disp)
{
	return de_feat_get_num_chns(disp);
}

int bsp_disp_feat_get_num_layers(unsigned int disp)
{
	return de_feat_get_num_layers(disp);
}

int bsp_disp_feat_get_num_layers_by_chn(unsigned int disp, unsigned int chn)
{
	return de_feat_get_num_layers_by_chn(disp, chn);
}

/*
 * Query whether specified timing controller support the output_type passed as parameter
 * @disp: the index of timing controller
 * @output_type: the display output type
 * On support, returns 1. Otherwise, returns 0.
 */
int bsp_disp_feat_is_supported_output_types(unsigned int disp, unsigned int output_type)
{
	return de_feat_is_supported_output_types(disp, output_type);
}

int disp_feat_is_support_smbl(unsigned int disp)
{
	return de_feat_is_support_smbl(disp);
}

int bsp_disp_feat_is_support_capture(unsigned int disp)
{
	return de_feat_is_support_wb(disp);
}

int disp_init_feat(void)
{
	return de_feat_init();
}
