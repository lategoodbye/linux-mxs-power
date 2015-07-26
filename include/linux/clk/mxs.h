/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_CLK_MXS_H
#define __LINUX_CLK_MXS_H

int mxs_saif_clkmux_select(unsigned int clkmux);

int mx23_hbus_is_autoslow_enabled(void);
int mx23_hbus_set_autoslow(int enable);

int mx28_hbus_is_autoslow_enabled(void);
int mx28_hbus_set_autoslow(int enable);

#endif
