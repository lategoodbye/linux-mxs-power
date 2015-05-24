/*
 * Freescale MXS on-chip LDO driver
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright (C) 2014 Stefan Wahren
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * Inspired by imx-bootlets
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/power/mxs_power.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

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

/* Powered by an external source when 5V is present.  This does not
   necessarily mean the external source is powered by 5V,but the chip needs
   to be aware that 5V is present. */
#define HW_POWER_EXTERNAL_SOURCE_5V		6

/* Powered by an external source when 5V is not present.This doesn't
   necessarily mean the external source is powered by the battery, but the
   chip needs to be aware that the battery is present */
#define HW_POWER_EXTERNAL_SOURCE_BATTERY	7

/* Unknown configuration.  This is an error. */
#define HW_POWER_UNKNOWN_SOURCE			8

#define BM_POWER_STS_VBUSVALID0_STATUS	BIT(15)
#define BM_POWER_STS_DC_OK		BIT(9)

#define BM_POWER_5VCTRL_ILIMIT_EQ_ZERO	BIT(2)
#define BM_POWER_5VCTRL_ENABLE_DCDC	BIT(0)

#define BM_POWER_LINREG_OFFSET_DCDC_MODE	BIT(1)

/* Regulator IDs */
#define MXS_DCDC	1
#define MXS_VDDIO	2
#define MXS_VDDA	3
#define MXS_VDDD	4

struct mxs_ldo_info {
	/* regulator descriptor */
	struct regulator_desc desc;

	/* regulator control register */
	unsigned int ctrl_reg;

	/* disable DC-DC output */
	unsigned int disable_fet_mask;

	/* steps between linreg output and DC-DC target */
	unsigned int linreg_offset_mask;
	u8 linreg_offset_shift;

	/* brownout interrupt status */
	unsigned int irq_bo;

	/* brownout enable interrupt */
	unsigned int enirq_bo;

	/* function which determine power source */
	u8 (*get_power_source)(struct regulator_dev *);
};

static inline u8 get_linreg_offset(struct mxs_ldo_info *ldo, u32 regs)
{
	return (regs & ldo->linreg_offset_mask) >> ldo->linreg_offset_shift;
}

static u8 get_vddio_power_source(struct regulator_dev *reg)
{
	struct mxs_ldo_info *ldo = rdev_get_drvdata(reg);
	u32 v5ctrl, status, base;
	u8 offset;

	if (regmap_read(reg->regmap, HW_POWER_5VCTRL, &v5ctrl))
		return HW_POWER_UNKNOWN_SOURCE;

	if (regmap_read(reg->regmap, HW_POWER_STS, &status))
		return HW_POWER_UNKNOWN_SOURCE;

	if (regmap_read(reg->regmap, ldo->ctrl_reg, &base))
		return HW_POWER_UNKNOWN_SOURCE;

	offset = get_linreg_offset(ldo, base);

	/* If VBUS valid then 5 V power supply present */
	if (status & BM_POWER_STS_VBUSVALID0_STATUS) {
		/* Powered by Linreg, DC-DC is off */
		if ((base & ldo->disable_fet_mask) &&
		    !(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)) {
			return HW_POWER_LINREG_DCDC_OFF;
		}

		if (v5ctrl & BM_POWER_5VCTRL_ENABLE_DCDC) {
			/* Powered by DC-DC, Linreg is on */
			if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)
				return HW_POWER_DCDC_LINREG_ON;
		} else {
			/* Powered by Linreg, DC-DC is off */
			if (!(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE))
				return HW_POWER_LINREG_DCDC_OFF;
		}
	} else {
		/* Powered by DC-DC, Linreg is on */
		if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)
			return HW_POWER_DCDC_LINREG_ON;
	}

	return HW_POWER_UNKNOWN_SOURCE;
}

static u8 get_vdda_vddd_power_source(struct regulator_dev *reg)
{
	struct mxs_ldo_info *ldo = rdev_get_drvdata(reg);
	struct regulator_desc *desc = &ldo->desc;
	u32 v5ctrl, status, base;
	u8 offset;

	if (regmap_read(reg->regmap, HW_POWER_5VCTRL, &v5ctrl))
		return HW_POWER_UNKNOWN_SOURCE;

	if (regmap_read(reg->regmap, HW_POWER_STS, &status))
		return HW_POWER_UNKNOWN_SOURCE;

	if (regmap_read(reg->regmap, ldo->ctrl_reg, &base))
		return HW_POWER_UNKNOWN_SOURCE;

	offset = get_linreg_offset(ldo, base);

	/* DC-DC output is disabled */
	if (base & ldo->disable_fet_mask) {
		/* Powered by Linreg, DC-DC is off */
		if (!(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE))
			return HW_POWER_LINREG_DCDC_OFF;
	}

	/* If VBUS valid then 5 V power supply present */
	if (status & BM_POWER_STS_VBUSVALID0_STATUS) {
		/* Powered by DC-DC, Linreg is on */
		if (v5ctrl & BM_POWER_5VCTRL_ENABLE_DCDC)
			return HW_POWER_DCDC_LINREG_ON;

		/* Powered by Linreg, DC-DC is off */
		return HW_POWER_LINREG_DCDC_OFF;
	}

	/* DC-DC is on */
	if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE) {
		/* Powered by DC-DC, Linreg is on */
		if (base & desc->enable_mask)
			return HW_POWER_DCDC_LINREG_ON;

		/* Powered by DC-DC, Linreg is off */
		return HW_POWER_DCDC_LINREG_OFF;
	}

	return HW_POWER_UNKNOWN_SOURCE;
}

static int mxs_ldo_set_voltage_sel(struct regulator_dev *reg, unsigned sel)
{
	struct mxs_ldo_info *ldo = rdev_get_drvdata(reg);
	struct regulator_desc *desc = &ldo->desc;
	u32 status = 0;
	u32 ctrl;
	int timeout;
	int ret;

	ret = regmap_read(reg->regmap, HW_POWER_CTRL, &ctrl);
	if (ret)
		return ret;

	ret = mxs_regmap_clr(reg->regmap, HW_POWER_CTRL, ldo->enirq_bo);
	if (ret)
		return ret;

	ret = regmap_update_bits(reg->regmap, desc->vsel_reg, desc->vsel_mask,
				 sel);
	if (ret)
		goto restore_bo;

	if (ldo->get_power_source) {
		switch (ldo->get_power_source(reg)) {
		case HW_POWER_LINREG_DCDC_OFF:
		case HW_POWER_LINREG_DCDC_READY:
			/*
			 * Since the DC-DC converter is off we can't
			 * trigger on DC_OK. So wait at least 1 ms
			 * for stabilization.
			 */
			usleep_range(1000, 2000);
			ret = 0;
			goto restore_bo;
		}
	}

	/* Make sure DC_OK has changed */
	usleep_range(15, 20);

	for (timeout = 0; timeout < 20; timeout++) {
		ret = regmap_read(reg->regmap, HW_POWER_STS, &status);

		if (ret)
			break;

		/* DC-DC converter control loop has stabilized */
		if (status & BM_POWER_STS_DC_OK)
			goto restore_bo;

		udelay(1);
	}

	if (!ret) {
		ret = -ETIMEDOUT;
		dev_warn_ratelimited(&reg->dev, "%s: timeout status=0x%08x\n",
				     __func__, status);
	}

	msleep(20);

restore_bo:

	mxs_regmap_clr(reg->regmap, HW_POWER_CTRL, ldo->irq_bo);

	if (ctrl & ldo->enirq_bo)
		mxs_regmap_set(reg->regmap, HW_POWER_CTRL, ldo->enirq_bo);

	return ret;
}

static struct regulator_ops mxs_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.set_voltage_sel	= mxs_ldo_set_voltage_sel,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct mxs_ldo_info imx23_info_vddio = {
	.desc = {
		.name = "vddio",
		.id = MXS_VDDIO,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 2800000,
		.vsel_reg = HW_POWER_VDDIOCTRL,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_reg = HW_POWER_5VCTRL,
		.enable_mask = 1 << 2,
		.disable_val = 1 << 2,
	},
	.ctrl_reg = HW_POWER_VDDIOCTRL,
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.irq_bo = 1 << 11,
	.enirq_bo = 1 << 10,
	.get_power_source = get_vddio_power_source,
};

static const struct mxs_ldo_info imx28_info_vddio = {
	.desc = {
		.name = "vddio",
		.id = MXS_VDDIO,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x11,
		.uV_step = 50000,
		.linear_min_sel = 0,
		.min_uV = 2800000,
		.vsel_reg = HW_POWER_VDDIOCTRL,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_reg = HW_POWER_5VCTRL,
		.enable_mask = 1 << 2,
		.disable_val = 1 << 2,
	},
	.ctrl_reg = HW_POWER_VDDIOCTRL,
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.irq_bo = 1 << 11,
	.enirq_bo = 1 << 10,
	.get_power_source = get_vddio_power_source,
};

static const struct mxs_ldo_info mxs_info_vdda = {
	.desc = {
		.name = "vdda",
		.id = MXS_VDDA,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 1500000,
		.vsel_reg = HW_POWER_VDDACTRL,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_reg = HW_POWER_VDDACTRL,
		.enable_mask = 1 << 17,
		.enable_val = 1 << 17,
	},
	.ctrl_reg = HW_POWER_VDDACTRL,
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.irq_bo = 1 << 9,
	.enirq_bo = 1 << 8,
	.get_power_source = get_vdda_vddd_power_source,
};

static const struct mxs_ldo_info mxs_info_vddd = {
	.desc = {
		.name = "vddd",
		.id = MXS_VDDD,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 800000,
		.vsel_reg = HW_POWER_VDDDCTRL,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_reg = HW_POWER_VDDDCTRL,
		.enable_mask = 1 << 21,
		.enable_val = 1 << 21,
	},
	.ctrl_reg = HW_POWER_VDDDCTRL,
	.disable_fet_mask = 1 << 20,
	.linreg_offset_mask = 3 << 16,
	.linreg_offset_shift = 16,
	.irq_bo = 1 << 7,
	.enirq_bo = 1 << 6,
	.get_power_source = get_vdda_vddd_power_source,
};

static const struct of_device_id of_mxs_regulator_ldo_match[] = {
	{ .compatible = "fsl,imx23-vddio", .data = &imx23_info_vddio },
	{ .compatible = "fsl,imx23-vdda",  .data = &mxs_info_vdda },
	{ .compatible = "fsl,imx23-vddd",  .data = &mxs_info_vddd },
	{ .compatible = "fsl,imx28-vddio", .data = &imx28_info_vddio },
	{ .compatible = "fsl,imx28-vdda",  .data = &mxs_info_vdda },
	{ .compatible = "fsl,imx28-vddd",  .data = &mxs_info_vddd },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_mxs_regulator_ldo_match);

static int mxs_regulator_ldo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct device_node *parent_np;
	struct regulator_dev *rdev = NULL;
	struct mxs_ldo_info *info;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };

	match = of_match_device(of_mxs_regulator_ldo_match, dev);
	if (!match) {
		/* We do not expect this to happen */
		dev_err(dev, "%s: Unable to match device\n", __func__);
		return -ENODEV;
	}

	info = devm_kmemdup(dev, match->data, sizeof(struct mxs_ldo_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	initdata = of_get_regulator_init_data(dev, dev->of_node, &info->desc);

	parent_np = of_get_parent(dev->of_node);
	if (!parent_np)
		return -ENODEV;
	config.regmap = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(config.regmap))
		return PTR_ERR(config.regmap);

	config.dev = dev;
	config.init_data = initdata;
	config.driver_data = info;
	config.of_node = dev->of_node;

	rdev = devm_regulator_register(dev, &info->desc, &config);
	if (IS_ERR(rdev)) {
		int ret = PTR_ERR(rdev);

		dev_err(dev, "%s: failed to register regulator(%d)\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static struct platform_driver mxs_regulator_ldo_driver = {
	.driver = {
		.name	= "mxs_regulator_ldo",
		.of_match_table = of_mxs_regulator_ldo_match,
	},
	.probe	= mxs_regulator_ldo_probe,
};

module_platform_driver(mxs_regulator_ldo_driver);

MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("Freescale MXS on-chip LDO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mxs_regulator_ldo");
