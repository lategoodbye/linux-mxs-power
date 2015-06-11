/*
 * Freescale MXS power subsystem defines
 *
 * Copyright (C) 2015 Stefan Wahren
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __POWER_MXS_POWER_H
#define __POWER_MXS_POWER_H

#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/stmp_device.h>

/* MXS power register address offset */
#define HW_POWER_CTRL		0x0000
#define HW_POWER_5VCTRL		0x0010
#define HW_POWER_VDDDCTRL	0x0040
#define HW_POWER_VDDACTRL	0x0050
#define HW_POWER_VDDIOCTRL	0x0060
#define HW_POWER_DCDC4P2	0x0080
#define HW_POWER_MISC		0x0090
#define HW_POWER_STS		0x00c0
#define HW_POWER_RESET		0x0100

static inline int mxs_regmap_set(struct regmap *map, unsigned int reg, unsigned int val)
{
	return regmap_write(map, reg + STMP_OFFSET_REG_SET, val);
}

static inline int mxs_regmap_clr(struct regmap *map, unsigned int reg, unsigned int val)
{
	return regmap_write(map, reg + STMP_OFFSET_REG_CLR, val);
}

struct mxs_power_data {
	struct power_supply *ac;
	struct regmap *regmap;

#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif
};

void mxs_power_init_device_debugfs(struct mxs_power_data *data);

void mxs_power_remove_device_debugfs(struct mxs_power_data *data);

#endif
