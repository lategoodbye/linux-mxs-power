/*
 * Freescale MXS regulators
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
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

#define BM_POWER_LINREG_OFFSET_DCDC_MODE	BIT(1)

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

#define SHIFT_FREQSEL			4

#define BM_POWER_MISC_FREQSEL		(7 << SHIFT_FREQSEL)

#define HW_POWER_MISC_FREQSEL_20000_KHZ	1
#define HW_POWER_MISC_FREQSEL_24000_KHZ	2
#define HW_POWER_MISC_FREQSEL_19200_KHZ	3

#define HW_POWER_MISC_SEL_PLLCLK	BIT(0)

#define MXS_DCDC	1
#define MXS_VDDIO	2
#define MXS_VDDA	3
#define MXS_VDDD	4

struct mxs_dcdc {
	struct regulator_desc desc;

	void __iomem *base_addr;
	void __iomem *status_addr;
	void __iomem *misc_addr;
};

struct mxs_ldo {
	struct regulator_desc desc;
	unsigned int disable_fet_mask;
	unsigned int linreg_offset_mask;
	u8 linreg_offset_shift;
	u8 (*get_power_source)(struct regulator_dev *);

	void __iomem *base_addr;
	void __iomem *status_addr;
	void __iomem *v5ctrl_addr;
};

static inline u8 get_linreg_offset(struct mxs_ldo *ldo, u32 regs)
{
	return (regs & ldo->linreg_offset_mask) >> ldo->linreg_offset_shift;
}

static u8 get_vddio_power_source(struct regulator_dev *reg)
{
	struct mxs_ldo *ldo = rdev_get_drvdata(reg);
	u32 v5ctrl, status, base;
	u8 offset;

	v5ctrl = readl(ldo->v5ctrl_addr);
	status = readl(ldo->status_addr);
	base = readl(ldo->base_addr);
	offset = get_linreg_offset(ldo, base);

	if (status & BM_POWER_STS_VBUSVALID0_STATUS) {
		if ((base & ldo->disable_fet_mask) &&
		    !(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)) {
			return HW_POWER_LINREG_DCDC_OFF;
		}

		if (v5ctrl & BM_POWER_5VCTRL_ENABLE_DCDC) {
			if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)
				return HW_POWER_DCDC_LINREG_ON;
		} else {
			if (!(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE))
				return HW_POWER_LINREG_DCDC_OFF;
		}
	} else {
		if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE)
			return HW_POWER_DCDC_LINREG_ON;
	}

	return HW_POWER_UNKNOWN_SOURCE;
}

static u8 get_vdda_vddd_power_source(struct regulator_dev *reg)
{
	struct mxs_ldo *ldo = rdev_get_drvdata(reg);
	struct regulator_desc *desc = &ldo->desc;
	u32 v5ctrl, status, base;
	u8 offset;

	v5ctrl = readl(ldo->v5ctrl_addr);
	status = readl(ldo->status_addr);
	base = readl(ldo->base_addr);
	offset = get_linreg_offset(ldo, base);

	if (base & ldo->disable_fet_mask) {
		if (status & BM_POWER_STS_VBUSVALID0_STATUS)
			return HW_POWER_EXTERNAL_SOURCE_5V;

		if (!(offset & BM_POWER_LINREG_OFFSET_DCDC_MODE))
			return HW_POWER_LINREG_DCDC_OFF;
	}

	if (status & BM_POWER_STS_VBUSVALID0_STATUS) {
		if (v5ctrl & BM_POWER_5VCTRL_ENABLE_DCDC)
			return HW_POWER_DCDC_LINREG_ON;

		return HW_POWER_LINREG_DCDC_OFF;
	}

	if (offset & BM_POWER_LINREG_OFFSET_DCDC_MODE) {
		if (base & desc->enable_mask)
			return HW_POWER_DCDC_LINREG_ON;

		return HW_POWER_DCDC_LINREG_OFF;
	}

	return HW_POWER_UNKNOWN_SOURCE;
}

int get_dcdc_clk_freq(struct mxs_dcdc *dcdc)
{
	int ret = -EINVAL;
	u32 val;

	val = readl(dcdc->misc_addr);

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

int set_dcdc_clk_freq(struct mxs_dcdc *dcdc, int khz)
{
	u32 val;
	int ret = 0;

	val = readl(dcdc->misc_addr);

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
	writel(val, dcdc->misc_addr);

	/* then set PLL as clk for DC-DC converter */
	writel(val | HW_POWER_MISC_SEL_PLLCLK, dcdc->misc_addr);

	return 0;
}

static int mxs_ldo_set_voltage_sel(struct regulator_dev *reg, unsigned sel)
{
	struct mxs_ldo *ldo = rdev_get_drvdata(reg);
	struct regulator_desc *desc = &ldo->desc;
	unsigned long start;
	u32 regs;
	u8 power_source = HW_POWER_UNKNOWN_SOURCE;

	regs = (readl(ldo->base_addr) & ~desc->vsel_mask);
	writel(sel | regs, ldo->base_addr);

	if (ldo->get_power_source)
		power_source = ldo->get_power_source(reg);

	switch (power_source) {
	case HW_POWER_LINREG_DCDC_OFF:
	case HW_POWER_LINREG_DCDC_READY:
	case HW_POWER_EXTERNAL_SOURCE_5V:
		usleep_range(1000, 2000);
		return 0;
	}

	usleep_range(15, 20);
	start = jiffies;
	while (1) {
		if (readl(ldo->status_addr) & BM_POWER_STS_DC_OK)
			return 0;

		if (time_after(jiffies, start +	msecs_to_jiffies(20)))
			break;

		schedule();
	}

	dev_warn_ratelimited(&reg->dev, "%s: timeout status=0x%08x\n",
			     __func__, readl(ldo->status_addr));

	return -ETIMEDOUT;
}

static int mxs_ldo_get_voltage_sel(struct regulator_dev *reg)
{
	struct mxs_ldo *ldo = rdev_get_drvdata(reg);

	return readl(ldo->base_addr) & ldo->desc.vsel_mask;
}

static int mxs_dcdc_is_enabled(struct regulator_dev *reg)
{
	struct mxs_dcdc *dcdc = rdev_get_drvdata(reg);

	if (readl(dcdc->base_addr) & BM_POWER_5VCTRL_ENABLE_DCDC)
		return 1;

	return 0;
}

static int mxs_ldo_is_enabled(struct regulator_dev *reg)
{
	struct mxs_ldo *ldo = rdev_get_drvdata(reg);
	u8 power_source = HW_POWER_UNKNOWN_SOURCE;

	if (ldo->get_power_source)
		power_source = ldo->get_power_source(reg);

	switch (power_source) {
	case HW_POWER_LINREG_DCDC_OFF:
	case HW_POWER_LINREG_DCDC_READY:
	case HW_POWER_DCDC_LINREG_ON:
		return 1;
	}

	return 0;
}

static struct regulator_ops mxs_dcdc_ops = {
	.is_enabled		= mxs_dcdc_is_enabled,
};

static struct regulator_ops mxs_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.set_voltage_sel	= mxs_ldo_set_voltage_sel,
	.get_voltage_sel	= mxs_ldo_get_voltage_sel,
	.is_enabled		= mxs_ldo_is_enabled,
};

static const struct mxs_dcdc mxs_info_dcdc = {
	.desc = {
		.name = "dcdc",
		.id = MXS_DCDC,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.ops = &mxs_dcdc_ops,
		.enable_mask = (1 << 0),
	},
};

static const struct mxs_ldo imx23_info_vddio = {
	.desc = {
		.name = "vddio",
		.id = MXS_VDDIO,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 2800000,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
	},
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.get_power_source = get_vddio_power_source,
};

static const struct mxs_ldo imx28_info_vddio = {
	.desc = {
		.name = "vddio",
		.id = MXS_VDDIO,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x11,
		.uV_step = 50000,
		.linear_min_sel = 0,
		.min_uV = 2800000,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
	},
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.get_power_source = get_vddio_power_source,
};

static const struct mxs_ldo mxs_info_vdda = {
	.desc = {
		.name = "vdda",
		.id = MXS_VDDA,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 1500000,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_mask = (1 << 17),
	},
	.disable_fet_mask = 1 << 16,
	.linreg_offset_mask = 3 << 12,
	.linreg_offset_shift = 12,
	.get_power_source = get_vdda_vddd_power_source,
};

static const struct mxs_ldo mxs_info_vddd = {
	.desc = {
		.name = "vddd",
		.id = MXS_VDDD,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 0x20,
		.uV_step = 25000,
		.linear_min_sel = 0,
		.min_uV = 800000,
		.vsel_mask = 0x1f,
		.ops = &mxs_ldo_ops,
		.enable_mask = (1 << 21),
	},
	.disable_fet_mask = 1 << 20,
	.linreg_offset_mask = 3 << 16,
	.linreg_offset_shift = 16,
	.get_power_source = get_vdda_vddd_power_source,
};

static const struct of_device_id of_mxs_regulator_match[] = {
	{ .compatible = "fsl,imx23-dcdc",  .data = &mxs_info_dcdc },
	{ .compatible = "fsl,imx28-dcdc",  .data = &mxs_info_dcdc },
	{ .compatible = "fsl,imx23-vddio", .data = &imx23_info_vddio },
	{ .compatible = "fsl,imx23-vdda",  .data = &mxs_info_vdda },
	{ .compatible = "fsl,imx23-vddd",  .data = &mxs_info_vddd },
	{ .compatible = "fsl,imx28-vddio", .data = &imx28_info_vddio },
	{ .compatible = "fsl,imx28-vdda",  .data = &mxs_info_vdda },
	{ .compatible = "fsl,imx28-vddd",  .data = &mxs_info_vddd },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_mxs_regulator_match);

struct regulator_dev *mxs_dcdc_register(struct platform_device *pdev, const void *data)
{
	struct device *dev = &pdev->dev;
	struct mxs_dcdc *dcdc;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	struct resource *res;
	char *pname;
	u32 dcdc_clk_freq;

	dcdc = devm_kmemdup(dev, data, sizeof(*dcdc), GFP_KERNEL);
	if (!dcdc)
		return NULL;

	pname = "base-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	dcdc->base_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(dcdc->base_addr))
		return NULL;

	/* status register is shared between the regulators */
	pname = "status-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	dcdc->status_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(dcdc->status_addr))
		return NULL;

	pname = "misc-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	dcdc->misc_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(dcdc->misc_addr))
		return NULL;

	initdata = of_get_regulator_init_data(dev, dev->of_node, &dcdc->desc);
	if (!initdata)
		return NULL;

	config.driver_data = dcdc;
	config.dev = dev;
	config.init_data = initdata;
	config.of_node = dev->of_node;

	pname = "switching-frequency";
	if (!of_property_read_u32(dev->of_node, pname, &dcdc_clk_freq))
		set_dcdc_clk_freq(dcdc, dcdc_clk_freq / 1000);

	dcdc_clk_freq = get_dcdc_clk_freq(dcdc);
	dev_info(dev, "DCDC clock freq: %d kHz\n", dcdc_clk_freq);

	return devm_regulator_register(dev, &dcdc->desc, &config);
}

struct regulator_dev *mxs_ldo_register(struct platform_device *pdev, const void *data)
{
	struct device *dev = &pdev->dev;
	struct mxs_ldo *ldo;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	struct resource *res;
	char *pname;

	ldo = devm_kmemdup(dev, data, sizeof(*ldo), GFP_KERNEL);
	if (!ldo)
		return NULL;

	pname = "base-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	ldo->base_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(ldo->base_addr))
		return NULL;

	/* status register is shared between the regulators */
	pname = "status-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	ldo->status_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(ldo->status_addr))
		return NULL;

	pname = "v5ctrl-address";
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pname);
	if (!res) {
		dev_err(dev, "Missing '%s' IO resource\n", pname);
		return NULL;
	}
	ldo->v5ctrl_addr = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
	if (IS_ERR(ldo->v5ctrl_addr))
		return NULL;

	initdata = of_get_regulator_init_data(dev, dev->of_node, &ldo->desc);
	if (!initdata)
		return NULL;

	config.dev = dev;
	config.init_data = initdata;
	config.driver_data = ldo;
	config.of_node = dev->of_node;

	return devm_regulator_register(dev, &ldo->desc, &config);
}

static int mxs_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct regulator_dev *rdev = NULL;
	int ret = 0;

	match = of_match_device(of_mxs_regulator_match, dev);
	if (!match) {
		/* We do not expect this to happen */
		dev_err(dev, "%s: Unable to match device\n", __func__);
		return -ENODEV;
	}

	if ((strcmp(match->compatible, "fsl,imx23-dcdc") == 0) ||
	    (strcmp(match->compatible, "fsl,imx28-dcdc") == 0)) {
		rdev = mxs_dcdc_register(pdev, match->data);
	} else {
		rdev = mxs_ldo_register(pdev, match->data);
	}

	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "%s: failed to register regulator(%d)\n",
			__func__, ret);
		return ret;
	}

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static struct platform_driver mxs_regulator_driver = {
	.driver = {
		.name	= "mxs_regulator",
		.of_match_table = of_mxs_regulator_match,
	},
	.probe	= mxs_regulator_probe,
};

module_platform_driver(mxs_regulator_driver);

MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("Freescale MXS regulators");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mxs_regulator");
