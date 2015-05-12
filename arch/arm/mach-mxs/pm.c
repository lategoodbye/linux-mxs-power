/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/userspace-consumer.h>
#include "pm.h"

static int mxs_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		cpu_do_idle();
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static struct platform_suspend_ops mxs_suspend_ops = {
	.enter = mxs_suspend_enter,
	.valid = suspend_valid_only_mem,
};

static struct platform_device mxs_cpufreq_pdev = {
	.name = "cpufreq-dt",
};

static struct regulator_bulk_data dcdc_bulk_data = {
	.supply = "dcdc",
};

static struct regulator_userspace_consumer_data userspace_consumer_data = {
	.name = "user",
	.num_supplies = 1,
	.supplies = &dcdc_bulk_data,
};

static struct platform_device userspace_consumer_device = {
	.name = "reg-userspace-consumer",
	.id = 0,
	.dev = {
		.platform_data = &userspace_consumer_data,
	},
};

void __init mxs_pm_init(void)
{
	suspend_set_ops(&mxs_suspend_ops);
	platform_device_register(&mxs_cpufreq_pdev);
	platform_device_register(&userspace_consumer_device);
}
