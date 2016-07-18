/*
 * Copyright 2015 - Stefan Wahren
 * Copyright 2013 - Digi International, Inc. All Rights Reserved.
 *
 * PSWITCH driver for Freescale MXS boards
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>

#define KEY_PRESSED		1
#define KEY_RELEASED		0
#define KEY_POLLING_PERIOD_MS	20

#define HW_POWER_CTRL			0x00000000
#define HW_POWER_STS			0x000000c0

#define BM_POWER_CTRL_ENIRQ_PSWITCH	BIT(17)
#define BM_POWER_CTRL_POLARITY_PSWITCH	BIT(18)
#define BM_POWER_CTRL_PSWITCH_IRQ_SRC	BIT(19)
#define BM_POWER_CTRL_PSWITCH_IRQ	BIT(20)

#define BM_POWER_STS_PSWITCH		(3 << 20)
#define BF_POWER_STS_PSWITCH(v)		(((v) << 20) & BM_POWER_STS_PSWITCH)

#define BM_POWER_PSWITCH_LOW_LEVEL	(0 << 20)
#define BM_POWER_PSWITCH_MID_LEVEL      (1 << 20)
#define BM_POWER_PSWITCH_HIGH_LEVEL     (3 << 20)

struct mxs_pswitch_data {
	struct input_dev *input;
	struct regmap *syscon;
	int irq;
	unsigned int input_code;
	struct delayed_work poll_key;
};

static int get_pswitch_level(struct mxs_pswitch_data *info)
{
	u32 val;
	int ret = regmap_read(info->syscon, HW_POWER_STS, &val);

	if (ret)
		return ret;

	return val & BM_POWER_STS_PSWITCH;
}

static void mxs_pswitch_work_func(struct work_struct *work)
{
	struct mxs_pswitch_data *info;
	int ret;

	info = container_of(work, struct mxs_pswitch_data, poll_key.work);
	ret = get_pswitch_level(info);

	switch (ret) {
	case BM_POWER_PSWITCH_LOW_LEVEL:
		input_report_key(info->input, info->input_code, KEY_RELEASED);
		input_sync(info->input);
		break;
	case BM_POWER_PSWITCH_MID_LEVEL:
	case BM_POWER_PSWITCH_HIGH_LEVEL:
		schedule_delayed_work(&info->poll_key,
			msecs_to_jiffies(KEY_POLLING_PERIOD_MS));
		break;
	default:
		dev_err(info->input->dev.parent,
			"Cannot read PSWITCH status: %d\n", ret);
		break;
	}
}

static irqreturn_t mxs_pswitch_irq_handler(int irq, void *dev_id)
{
	struct mxs_pswitch_data *info = dev_id;
	u32 val;
	int ret = regmap_read(info->syscon, HW_POWER_CTRL, &val);

	val &= BM_POWER_CTRL_PSWITCH_IRQ;

	/* check if irq by power key */
	if (ret || !val)
		return IRQ_HANDLED;

	pm_wakeup_event(info->input->dev.parent, 0);

	/* Ack the irq */
	regmap_write(info->syscon, HW_POWER_CTRL + STMP_OFFSET_REG_CLR,
		     BM_POWER_CTRL_PSWITCH_IRQ);

	input_report_key(info->input, info->input_code, KEY_PRESSED);
	input_sync(info->input);

	/* schedule the work to poll the key for key-release event */
	schedule_delayed_work(&info->poll_key,
		msecs_to_jiffies(KEY_POLLING_PERIOD_MS));

	return IRQ_HANDLED;
}

static int mxs_pswitch_hwinit(struct platform_device *pdev)
{
	struct mxs_pswitch_data *info = platform_get_drvdata(pdev);
	int ret;

	ret = regmap_write(info->syscon, HW_POWER_CTRL + STMP_OFFSET_REG_CLR,
			   BM_POWER_CTRL_PSWITCH_IRQ);
	if (ret)
		return ret;

	ret = regmap_write(info->syscon, HW_POWER_CTRL + STMP_OFFSET_REG_SET,
			   BM_POWER_CTRL_POLARITY_PSWITCH |
			   BM_POWER_CTRL_ENIRQ_PSWITCH);
	if (ret)
		return ret;

	ret = regmap_write(info->syscon, HW_POWER_CTRL + STMP_OFFSET_REG_CLR,
			   BM_POWER_CTRL_PSWITCH_IRQ);

	return ret;
}

static int mxs_pswitch_probe(struct platform_device *pdev)
{
	struct mxs_pswitch_data *info;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;
	int ret = 0;

	/* Create and register the input driver. */
	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	parent_np = of_get_parent(np);
	if (!parent_np)
		return -ENODEV;
	info->syscon = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(info->syscon))
		return PTR_ERR(info->syscon);

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "linux,code", &info->input_code))
		info->input_code = KEY_POWER;

	info->input = devm_input_allocate_device(dev);
	if (!info->input)
		return -ENOMEM;

	info->input->name = "mxs-pswitch";
	info->input->phys = "mxs_pswitch/input0";
	info->input->id.bustype = BUS_HOST;
	info->input->dev.parent = &pdev->dev;

	input_set_capability(info->input, EV_KEY, info->input_code);

	platform_set_drvdata(pdev, info);

	INIT_DELAYED_WORK(&info->poll_key, mxs_pswitch_work_func);

	ret = mxs_pswitch_hwinit(pdev);
	if (ret) {
		dev_err(dev, "Can't init hardware: %d\n", ret);
		goto err;
	}

	ret = devm_request_any_context_irq(dev, info->irq,
					   mxs_pswitch_irq_handler,
					   IRQF_SHARED, "mxs-pswitch", info);
	if (ret) {
		dev_err(dev, "Can't get IRQ for pswitch: %d\n", ret);
		goto err;
	}

	ret = input_register_device(info->input);
	if (ret) {
		dev_err(dev, "Can't register input device: %d\n", ret);
		goto err;
	}

	device_init_wakeup(dev, true);

	return 0;

err:
	cancel_delayed_work_sync(&info->poll_key);

	return ret;
}

static int mxs_pswitch_remove(struct platform_device *pdev)
{
	struct mxs_pswitch_data *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->poll_key);

	return 0;
}

static const struct of_device_id mxs_pswitch_of_match[] = {
	{ .compatible = "fsl,imx23-pswitch" },
	{ .compatible = "fsl,imx28-pswitch" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, mxs_pswitch_of_match);

static struct platform_driver mxs_pswitch_driver = {
	.driver		= {
		.name   = "mxs-pswitch",
		.of_match_table = mxs_pswitch_of_match,
	},
	.probe	= mxs_pswitch_probe,
	.remove	= mxs_pswitch_remove,
};

module_platform_driver(mxs_pswitch_driver);

MODULE_AUTHOR("Digi International Inc");
MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_DESCRIPTION("MXS Power Switch Key driver");
MODULE_LICENSE("GPL");
