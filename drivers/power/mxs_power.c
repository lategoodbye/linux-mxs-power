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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/mxs_power.h>
#include <linux/stmp_device.h>
#include <linux/types.h>

#define BM_POWER_CTRL_POLARITY_VBUSVALID	BIT(5)
#define BM_POWER_CTRL_VBUSVALID_IRQ		BIT(4)
#define BM_POWER_CTRL_ENIRQ_VBUS_VALID		BIT(3)

#define BM_POWER_5VCTRL_VBUSVALID_THRESH	(7 << 8)
#define BM_POWER_5VCTRL_PWDN_5VBRNOUT		BIT(7)
#define BM_POWER_5VCTRL_ENABLE_LINREG_ILIMIT	BIT(6)
#define BM_POWER_5VCTRL_VBUSVALID_5VDETECT	BIT(4)

#define HW_POWER_5VCTRL_VBUSVALID_THRESH_4_40V	(5 << 8)

#define BM_POWER_STS_VBUSVALID0_STATUS		BIT(15)
#define BM_POWER_STS_VDD5V_DROOP		BIT(4)

#define STATUS_5V_CONNECTION	BIT(0)
#define STATUS_5V_NEW		BIT(1)

#define STATUS_NEW_5V_CONNECTION		(STATUS_5V_NEW | STATUS_5V_CONNECTION)
#define STATUS_NEW_5V_DISCONNECTION		STATUS_5V_NEW
#define STATUS_EXISTING_5V_CONNECTION		STATUS_5V_CONNECTION
#define STATUS_EXISTING_5V_DISCONNECTION	0

static enum power_supply_property mxs_power_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int mxs_power_5v_status(struct regmap *map)
{
	u32 ctrl = 0;
	u32 status = 0;
	int ret;

	ret = regmap_read(map, HW_POWER_CTRL, &ctrl);
	if (ret)
		return ret;

	ret = regmap_read(map, HW_POWER_STS, &status);
	if (ret)
		return ret;

	if (ctrl & BM_POWER_CTRL_POLARITY_VBUSVALID) {
		if ((ctrl & BM_POWER_CTRL_VBUSVALID_IRQ) ||
		    (status & BM_POWER_STS_VBUSVALID0_STATUS))
			return STATUS_NEW_5V_CONNECTION;

		return STATUS_EXISTING_5V_DISCONNECTION;
	}
	
	if ((ctrl & BM_POWER_CTRL_VBUSVALID_IRQ) ||
	    !(status & BM_POWER_STS_VBUSVALID0_STATUS) ||
	    (status & BM_POWER_STS_VDD5V_DROOP))
		return STATUS_NEW_5V_DISCONNECTION;

	return STATUS_EXISTING_5V_CONNECTION;
}

static void mxs_5v_work_func(struct work_struct *work)
{
	struct mxs_power_data *data =
		container_of(work, struct mxs_power_data, poll_5v.work);

	switch (mxs_power_5v_status(data->regmap)) {
	case STATUS_NEW_5V_CONNECTION:
	case STATUS_EXISTING_5V_CONNECTION:
		mxs_regmap_clr(data->regmap, HW_POWER_CTRL,
			       BM_POWER_CTRL_POLARITY_VBUSVALID);
		break;
	case STATUS_NEW_5V_DISCONNECTION:
	case STATUS_EXISTING_5V_DISCONNECTION:
		mxs_regmap_set(data->regmap, HW_POWER_CTRL,
			       BM_POWER_CTRL_POLARITY_VBUSVALID);
		break;
	default:
		return;
	}
	
	mxs_regmap_clr(data->regmap, HW_POWER_CTRL,
		       BM_POWER_CTRL_VBUSVALID_IRQ);

	mxs_regmap_set(data->regmap, HW_POWER_CTRL,
		       BM_POWER_CTRL_ENIRQ_VBUS_VALID);
}

static irqreturn_t mxs_irq_vdd5v(int irq, void *cookie)
{
	struct mxs_power_data *data = (struct mxs_power_data *)cookie;

	switch (mxs_power_5v_status(data->regmap)) {
	case STATUS_NEW_5V_CONNECTION:
		pr_info("New 5v connection detected\n");
		break;
	case STATUS_NEW_5V_DISCONNECTION:
		pr_info("New 5v disconnection detected\n");
		break;
	default:
		return IRQ_HANDLED;
	}

	mxs_regmap_clr(data->regmap, HW_POWER_CTRL,
		       BM_POWER_CTRL_ENIRQ_VBUS_VALID);

	schedule_delayed_work(&data->poll_5v, msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static int mxs_power_ac_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct mxs_power_data *data = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = mxs_power_5v_status(data->regmap);
		if (IS_ERR(ret))
			return ret;

		val->intval = (ret & STATUS_5V_CONNECTION) ? 1 : 0;
		ret = 0;
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
	.properties	= mxs_power_ac_props,
	.num_properties	= ARRAY_SIZE(mxs_power_ac_props),
	.get_property	= mxs_power_ac_get_property,
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
};

static int mxs_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mxs_power_data *data;
	struct power_supply_config psy_cfg = {};
	int ret;
	unsigned int irq;

	if (!np) {
		dev_err(dev, "missing device tree\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	/* Make sure the current limit of the linregs are disabled. */
	mxs_regmap_clr(data->regmap, HW_POWER_5VCTRL,
		       BM_POWER_5VCTRL_ENABLE_LINREG_ILIMIT);

	psy_cfg.drv_data = data;
	platform_set_drvdata(pdev, data);

	INIT_DELAYED_WORK(&data->poll_5v, mxs_5v_work_func);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq, mxs_irq_vdd5v, IRQF_SHARED,
			       "mxs-power", data);
	if (ret)
		return ret;

	data->ac = devm_power_supply_register(dev, &ac_desc, &psy_cfg);
	if (IS_ERR(data->ac))
		return PTR_ERR(data->ac);

	switch (mxs_power_5v_status(data->regmap)) {
	case STATUS_NEW_5V_CONNECTION:
	case STATUS_EXISTING_5V_CONNECTION:
		dev_info(dev, "5V = connected\n");
		break;
	case STATUS_NEW_5V_DISCONNECTION:
	case STATUS_EXISTING_5V_DISCONNECTION:
		dev_info(dev, "5V = disconnected\n");
		break;
	}

	mxs_power_init_device_debugfs(data);

	return of_platform_populate(np, NULL, NULL, dev);
}

static int mxs_power_remove(struct platform_device *pdev)
{
	struct mxs_power_data *data = platform_get_drvdata(pdev);

	mxs_power_remove_device_debugfs(data);
	cancel_delayed_work(&data->poll_5v);

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
