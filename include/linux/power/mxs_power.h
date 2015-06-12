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

/* Regulator IDs */
#define MXS_POWER_DCDC	1
#define MXS_POWER_VDDIO	2
#define MXS_POWER_VDDA	3
#define MXS_POWER_VDDD	4

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

/* Powered by linear regulator.  DCDC output is gated off and
   the linreg output is equal to the target. */
#define HW_POWER_LINREG_DCDC_OFF		1

/* Powered by linear regulator.  DCDC output is not gated off
   and is ready for the automatic hardware transistion after a 5V
   event.  The converters are not enabled when 5V is present. LinReg output
   is 25mV below target. */
#define HW_POWER_LINREG_DCDC_READY		2

/* Powered by DCDC converter and the LinReg is on. LinReg output
   is 25mV below target. */
#define HW_POWER_DCDC_LINREG_ON			3

/* Powered by DCDC converter and the LinReg is off. LinReg output
   is 25mV below target. */
#define HW_POWER_DCDC_LINREG_OFF		4

/* Powered by DCDC converter and the LinReg is ready for the
   automatic hardware transfer.  The LinReg output is not enabled and
   depends on the 5V presence to enable the LinRegs.  LinReg offset is 25mV
   below target. */
#define HW_POWER_DCDC_LINREG_READY		5

/* Powered by an external source when 5V is present. This does not
   necessarily mean the external source is powered by 5V,but the chip needs
   to be aware that 5V is present. */
#define HW_POWER_EXTERNAL_SOURCE_5V		6

/* Powered by an external source when 5V is not present. This doesn't
   necessarily mean the external source is powered by the battery, but the
   chip needs to be aware that the battery is present */
#define HW_POWER_EXTERNAL_SOURCE_BATTERY	7

/* Unknown configuration.  This is an error. */
#define HW_POWER_UNKNOWN_SOURCE			8

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
	struct delayed_work poll_5v;

#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif
};

void mxs_power_init_device_debugfs(struct mxs_power_data *data);

void mxs_power_remove_device_debugfs(struct mxs_power_data *data);

#endif
