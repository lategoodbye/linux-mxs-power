/*
 * Freescale MXS on-chip DC-DC driver
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright (C) 2015 Stefan Wahren
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

#define SHIFT_FREQSEL			4

#define BM_POWER_MISC_FREQSEL		(7 << SHIFT_FREQSEL)

/* Recommended DC-DC clock source values */
#define HW_POWER_MISC_FREQSEL_20000_KHZ	1
#define HW_POWER_MISC_FREQSEL_24000_KHZ	2
#define HW_POWER_MISC_FREQSEL_19200_KHZ	3

#define HW_POWER_MISC_SEL_PLLCLK	BIT(0)

struct mxs_dcdc_info {
	/* regulator descriptor */
	struct regulator_desc desc;

	/* regulator control register */
	int ctrl_reg;
};

static int mxs_set_dcdc_freq(struct regulator_dev *reg, u32 hz)
{
	u32 val;
	int ret;

	ret = regmap_read(reg->regmap, HW_POWER_MISC, &val);
	if (ret)
		return ret;

	val &= ~BM_POWER_MISC_FREQSEL;
	val &= ~HW_POWER_MISC_SEL_PLLCLK;

	/*
	 * Select the PLL/PFD based frequency that the DC-DC converter uses.
	 * The actual switching frequency driving the power inductor is
	 * DCDC_CLK/16. Accept only values recommend by Freescale.
	 */
	switch (hz) {
	case 1200000:
		val |= HW_POWER_MISC_FREQSEL_19200_KHZ << SHIFT_FREQSEL;
		break;
	case 1250000:
		val |= HW_POWER_MISC_FREQSEL_20000_KHZ << SHIFT_FREQSEL;
		break;
	case 1500000:
		val |= HW_POWER_MISC_FREQSEL_24000_KHZ << SHIFT_FREQSEL;
		break;
	default:
		dev_warn(&reg->dev, "Switching freq: %u Hz not supported\n",
			 hz);
		return -EINVAL;
	}

	/* First program FREQSEL */
	ret = regmap_write(reg->regmap, HW_POWER_MISC, val);
	if (ret)
		return ret;

	/* then set PLL as clock for DC-DC converter */
	val |= HW_POWER_MISC_SEL_PLLCLK;

	return regmap_write(reg->regmap, HW_POWER_MISC, val);
}

static struct regulator_ops mxs_dcdc_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct mxs_dcdc_info mxs_dcdc = {
	.desc = {
		.name = "dcdc",
		.id = MXS_POWER_DCDC,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.ops = &mxs_dcdc_ops,
		.enable_reg = HW_POWER_STS,
		.enable_mask = (1 << 0),
	},
};

static const struct of_device_id of_mxs_regulator_dcdc_match[] = {
	{ .compatible = "fsl,imx23-dcdc",  .data = &mxs_dcdc },
	{ .compatible = "fsl,imx28-dcdc",  .data = &mxs_dcdc },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_mxs_regulator_dcdc_match);

static int mxs_regulator_dcdc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct device_node *parent_np;
	struct regulator_dev *rdev = NULL;
	struct mxs_dcdc_info *info;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	u32 switch_freq;

	match = of_match_device(of_mxs_regulator_dcdc_match, dev);
	if (!match) {
		/* We do not expect this to happen */
		dev_err(dev, "%s: Unable to match device\n", __func__);
		return -ENODEV;
	}

	info = devm_kmemdup(dev, match->data, sizeof(struct mxs_dcdc_info),
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

	if (!of_property_read_u32(dev->of_node, "switching-frequency",
				  &switch_freq))
		mxs_set_dcdc_freq(rdev, switch_freq);

	rdev = devm_regulator_register(dev, &info->desc, &config);
	if (IS_ERR(rdev)) {
		int ret = PTR_ERR(rdev);

		dev_err(dev, "%s: failed to register regulator(%d)\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static struct platform_driver mxs_regulator_dcdc_driver = {
	.driver = {
		.name	= "mxs_regulator_dcdc",
		.of_match_table = of_mxs_regulator_dcdc_match,
	},
	.probe	= mxs_regulator_dcdc_probe,
};

module_platform_driver(mxs_regulator_dcdc_driver);

MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("Freescale MXS on-chip DC-DC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mxs_regulator_dcdc");
