/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __MXS_SLEEP_H__
#define __MXS_SLEEP_H__

#define MXS_DO_SW_OSC_RTC_TO_BATT       0
#define MXS_DONOT_SW_OSC_RTC_TO_BATT    1

#ifndef __ASSEMBLER__
extern const u32 mx23_cpu_standby_sz;
void mx23_cpu_standby(int arg1, void *arg2);

extern const u32 mx28_cpu_standby_sz;
void mx28_cpu_standby(int arg1, void *arg2);
#endif /* __ASSEMBLER__ */

#endif /* __MXS_SLEEP_H__ */
