/*
 * Freescale MXS power subsystem
 *
 * Copyright (C) 2014 Stefan Wahren
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

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/types.h>

#define HW_POWER_CTRL_CLR	0x08

#define BM_POWER_CTRL_POLARITY_VBUSVALID	BIT(5)
#define BM_POWER_CTRL_VBUSVALID_IRQ		BIT(4)
#define BM_POWER_CTRL_ENIRQ_VBUS_VALID		BIT(3)

#define HW_POWER_5VCTRL_OFFSET	0x10
#define HW_POWER_MISC_OFFSET	0x90

#define BM_POWER_5VCTRL_VBUSVALID_THRESH	(7 << 8)
#define BM_POWER_5VCTRL_PWDN_5VBRNOUT		BIT(7)
#define BM_POWER_5VCTRL_ENABLE_LINREG_ILIMIT	BIT(6)
#define BM_POWER_5VCTRL_VBUSVALID_5VDETECT	BIT(4)

#define HW_POWER_5VCTRL_VBUSVALID_THRESH_4_40V	(5 << 8)

#define SHIFT_FREQSEL	4

#define BM_POWER_MISC_FREQSEL			(7 << SHIFT_FREQSEL)

#define HW_POWER_MISC_FREQSEL_20000_KHZ		1
#define HW_POWER_MISC_FREQSEL_24000_KHZ		2
#define HW_POWER_MISC_FREQSEL_19200_KHZ		3

#define HW_POWER_MISC_SEL_PLLCLK		BIT(0)

static int dcdc_pll;
module_param(dcdc_pll, int, 0);
MODULE_PARM_DESC(dcdc_pll,
		 "DC-DC PLL frequency (kHz). Use 19200, 20000 or 24000");

struct mxs_power_data {
	void __iomem *base_addr;
	struct power_supply *ac;
};

int get_dcdc_clk_freq(struct mxs_power_data *pdata)
{
	void __iomem *base = pdata->base_addr;
	int ret = -EINVAL;
	u32 val;

	val = readl(base + HW_POWER_MISC_OFFSET);

	/* XTAL source */
	if ((val & HW_POWER_MISC_SEL_PLLCLK) == 0)
		return 24000;

	switch ((val & BM_POWER_MISC_FREQSEL) >> SHIFT_FREQSEL) {
	case HW_POWER_MISC_FREQSEL_20000_KHZ:
		ret = 20000;
		break;
	case HW_POWER_MISC_FREQSEL_24000_KHZ:
		ret = 24000;
		break;
	case HW_POWER_MISC_FREQSEL_19200_KHZ:
		ret = 19200;
		break;
	}

	return ret;
}

int set_dcdc_clk_freq(struct mxs_power_data *pdata, int khz)
{
	void __iomem *misc = pdata->base_addr + HW_POWER_MISC_OFFSET;
	u32 val;
	int ret = 0;

	val = readl(misc);

	val &= ~BM_POWER_MISC_FREQSEL;
	val &= ~HW_POWER_MISC_SEL_PLLCLK;

	/* Accept only values recommend by Freescale */
	switch (khz) {
	case 19200:
		val |= HW_POWER_MISC_FREQSEL_19200_KHZ << SHIFT_FREQSEL;
		break;
	case 20000:
		val |= HW_POWER_MISC_FREQSEL_20000_KHZ << SHIFT_FREQSEL;
		break;
	case 24000:
		val |= HW_POWER_MISC_FREQSEL_24000_KHZ << SHIFT_FREQSEL;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	/* First program FREQSEL */
	writel(val, misc);

	/* then set PLL as clk for DC-DC converter */
	writel(val | HW_POWER_MISC_SEL_PLLCLK, misc);

	return 0;
}

static enum power_supply_property mxs_power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int mxs_power_ac_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct mxs_power_data *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct of_device_id of_mxs_power_match[] = {
	{ .compatible = "fsl,imx23-power" },
	{ .compatible = "fsl,imx28-power" },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_mxs_power_match);

static const struct power_supply_desc ac_desc = {
       .properties     = mxs_power_ac_props,
       .num_properties = ARRAY_SIZE(mxs_power_ac_props),
       .get_property   = mxs_power_ac_get_property,
       .name           = "ac",
       .type           = POWER_SUPPLY_TYPE_MAINS,
};

static int mxs_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	struct mxs_power_data *data;
	struct power_supply_config psy_cfg = {};
	void __iomem *v5ctrl_addr;
	int dcdc_clk_freq;

	if (!np) {
		dev_err(dev, "missing device tree\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base_addr))
		return PTR_ERR(data->base_addr);

	psy_cfg.drv_data = data;

	data->ac = power_supply_register(dev, &ac_desc, &psy_cfg);
	if (IS_ERR(data->ac))
		return PTR_ERR(data->ac);

	platform_set_drvdata(pdev, data);

	if (dcdc_pll)
		set_dcdc_clk_freq(data, dcdc_pll);

	dcdc_clk_freq = get_dcdc_clk_freq(data);

	dev_info(dev, "DCDC clock freq: %d kHz\n", dcdc_clk_freq);

	v5ctrl_addr = data->base_addr + HW_POWER_5VCTRL_OFFSET;

	/* Make sure the current limit of the linregs are disabled. */
	writel(BM_POWER_5VCTRL_ENABLE_LINREG_ILIMIT,
	       v5ctrl_addr + HW_POWER_CTRL_CLR);

	return of_platform_populate(np, NULL, NULL, dev);
}

static int mxs_power_remove(struct platform_device *pdev)
{
	struct mxs_power_data *data = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);
	power_supply_unregister(data->ac);

	return 0;
}

static struct platform_driver mxs_power_driver = {
	.driver = {
		.name	= "mxs_power",
		.of_match_table = of_mxs_power_match,
	},
	.probe	= mxs_power_probe,
	.remove = mxs_power_remove,
};

module_platform_driver(mxs_power_driver);

MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("Freescale MXS power subsystem");
MODULE_LICENSE("GPL v2");
