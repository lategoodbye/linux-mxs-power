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

#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/tlbflush.h>

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/suspend.h>

#include "mxs-sleep.h"
#include "pm.h"

#define HW_CLKCTRL_CLKSEQ		0x000001d0
#define HW_CLKCTRL_XTAL			0x00000080
#define HW_POWER_RESET			0x00000100

#define BM_POWER_CTRL_ENIRQ_PSWITCH	0x00020000
#define BM_POWER_CTRL_PSWITCH_IRQ	0x00100000
#define HW_POWER_CTRL			0x00000000

#define HW_ICOLL_STAT			0x00000070

#define MXS_SET_ADDR			0x4
#define MXS_CLR_ADDR			0x8
#define MXS_TOG_ADDR			0xc

struct mxs_virt_addr_t {
	void __iomem *clkctrl_addr;
	void __iomem *power_addr;
	void __iomem *dram_addr;
	void __iomem *pinctrl_addr;
	void __iomem *emi_addr;
	void __iomem *icoll_addr;
	/* These are not used */
	void __iomem *rtc_addr;
} __aligned(8);

struct mxs_pm_socdata {
	const char *clkctrl_compat;
	const char *power_compat;
	const char *dram_compat;
	const char *pinctrl_compat;
	const char *emi_compat;
	const char *icoll_compat;
	const char *rtc_compat;
	void (*suspend_asm)(int arg1, void *arg2);
	const u32 *suspend_asm_sz;
};

static const struct mxs_pm_socdata imx28_pm_data __initconst = {
	.clkctrl_compat = "fsl,imx28-clkctrl",
	.power_compat = "fsl,imx28-power",
	.dram_compat = "fsl,imx28-dram",
	.pinctrl_compat = "fsl,imx28-pinctrl",
	.emi_compat = "fsl,imx28-emi",
	.icoll_compat = "fsl,imx28-icoll",
	.rtc_compat = "fsl,imx28-rtc",
	.suspend_asm = &mx28_cpu_standby,
	.suspend_asm_sz = &mx28_cpu_standby_sz,
};

static struct clk *cpu_clk;
static struct clk *osc_clk;
static struct clk *hbus_clk;
static unsigned long ocram_base;
static struct gen_pool *ocram_pool = NULL;
static void __iomem *suspend_ocram_base;
static const struct mxs_pm_socdata *soc_data;
static void (*mxs_suspend_in_ocram_fn)(int arg1, void *arg2);

static inline void __mxs_setl(u32 mask, void __iomem *reg)
{
	writel(mask, reg + MXS_SET_ADDR);
}

static inline void __mxs_clrl(u32 mask, void __iomem *reg)
{
	writel(mask, reg + MXS_CLR_ADDR);
}

static void get_virt_addr(const char *compat, void __iomem **paddr)
{
	struct device_node *np;
	struct resource res;

	np = of_find_compatible_node(NULL, NULL, compat);
	of_address_to_resource(np, 0, &res);
	*paddr = ioremap(res.start, resource_size(&res));
	WARN_ON(!*paddr);
}

static void mxs_do_standby(void)
{
	struct mxs_virt_addr_t *mxs_virt_addr = suspend_ocram_base;
	struct clk *cpu_parent = NULL;
	unsigned long cpu_rate = 0;
	unsigned long cpu_xtal_rate = 0;
	unsigned long hbus_rate = 0;
	u32 reg_clkseq, reg_xtal, reg_pwrctrl;
	int suspend_param = MXS_DONOT_SW_OSC_RTC_TO_BATT;

	/*
	 * 1) switch clock domains from PLL to 24MHz
	 * 2) lower voltage (TODO)
	 * 3) switch EMI to 24MHz and turn PLL off (done in sleep.S)
	 */

	/* make sure SRAM copy gets physically written into SDRAM.
	 * SDRAM will be placed into self-refresh during power down
	 */
	local_flush_tlb_all();
	flush_cache_all();

	/* now switch the CPU to cpu_xtal */
	cpu_rate = clk_get_rate(cpu_clk);
	cpu_xtal_rate = clk_get_rate(osc_clk);
	cpu_parent = clk_get_parent(cpu_clk);
	if (IS_ERR(cpu_parent)) {
		pr_err("%s: failed to get cpu parent with %ld\n",
		       __func__, PTR_ERR(cpu_parent));
		return;
	}
	hbus_rate = clk_get_rate(hbus_clk);

	if (clk_set_parent(cpu_clk, osc_clk) < 0) {
		pr_err("%s: failed to switch cpu clocks.", __func__);
		return;
	}

	/* Enable ENIRQ_PSWITCH */
	reg_pwrctrl = readl(mxs_virt_addr->power_addr + HW_POWER_CTRL);
	if (!(reg_pwrctrl & BM_POWER_CTRL_ENIRQ_PSWITCH)) {
		__mxs_setl(BM_POWER_CTRL_ENIRQ_PSWITCH,
			   mxs_virt_addr->power_addr + HW_POWER_CTRL);
	}

	reg_clkseq = readl(mxs_virt_addr->clkctrl_addr + HW_CLKCTRL_CLKSEQ);
	reg_xtal = readl(mxs_virt_addr->clkctrl_addr + HW_CLKCTRL_XTAL);

	/* do suspend */
	mxs_suspend_in_ocram_fn(suspend_param, mxs_virt_addr);

	writel(reg_clkseq, mxs_virt_addr->clkctrl_addr + HW_CLKCTRL_CLKSEQ);
	writel(reg_xtal, mxs_virt_addr->clkctrl_addr + HW_CLKCTRL_XTAL);

	/* Restore ENIRQ_PSWITCH */
	if (reg_pwrctrl & BM_POWER_CTRL_ENIRQ_PSWITCH) {
		__mxs_setl(BM_POWER_CTRL_ENIRQ_PSWITCH,
			   mxs_virt_addr->power_addr + HW_POWER_CTRL);
	} else {
		__mxs_clrl(BM_POWER_CTRL_PSWITCH_IRQ,
			   mxs_virt_addr->power_addr + HW_POWER_CTRL);
		__mxs_clrl(BM_POWER_CTRL_ENIRQ_PSWITCH,
			   mxs_virt_addr->power_addr + HW_POWER_CTRL);
	}

	if (clk_set_parent(cpu_clk, cpu_parent) < 0)
		pr_err("%s: Failed to switch cpu clock back.\n", __func__);

	clk_set_rate(cpu_clk, cpu_rate);
	clk_set_rate(hbus_clk, hbus_rate);
}

static int mxs_suspend_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
	case PM_SUSPEND_STANDBY:
		if (ocram_pool) {
			mxs_do_standby();
		} else {
			cpu_do_idle();
		}
 		break;
 	default:
 		return -EINVAL;
 	}
 	return 0;
 }
 
static int mxs_pm_valid(suspend_state_t state)
{
	return ((state == PM_SUSPEND_STANDBY) || (state == PM_SUSPEND_MEM));
}

static struct platform_suspend_ops mxs_suspend_ops = {
	.enter = mxs_suspend_enter,
	.valid = mxs_pm_valid,
};

static int __init mxs_suspend_alloc_ocram(size_t size, void __iomem **virt_out)
{
	struct device_node *node;
	struct platform_device *pdev;
	void __iomem *virt;
	phys_addr_t phys;
	int ret = 0;

	node = of_find_compatible_node(NULL, NULL, "mmio-sram");
	if (!node) {
		pr_warn("%s: failed to find ocram node!\n", __func__);
		return -ENODEV;
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		pr_warn("%s: failed to find ocram device!\n", __func__);
		ret = -ENODEV;
		goto put_node;
	}

	ocram_pool = gen_pool_get(&pdev->dev, NULL);
	if (!ocram_pool) {
		pr_warn("%s: ocram pool unavailable!\n", __func__);
		ret = -ENODEV;
		goto put_node;
	}

	ocram_base = gen_pool_alloc(ocram_pool, size);
	if (!ocram_base) {
		pr_warn("%s: unable to alloc ocram!\n", __func__);
		ret = -ENOMEM;
		goto put_node;
	}

	phys = gen_pool_virt_to_phys(ocram_pool, ocram_base);
	virt = __arm_ioremap_exec(phys, size, false);
	if (virt_out)
		*virt_out = virt;

put_node:
	of_node_put(node);

	return ret;
}

static int __init mxs_suspend_init(void)
 {
	struct device_node *np;
	struct mxs_virt_addr_t *mxs_virt_addr;
	/* Need this to avoid compile error due to const typeof in fncpy.h */
	void (*suspend_asm)(int arg1, void *arg2);
	int ret;

	np = of_find_compatible_node(NULL, NULL, "fsl,clkctrl");

	if (of_device_is_compatible(np, imx28_pm_data.clkctrl_compat))
		soc_data = &imx28_pm_data;
	else
		soc_data = NULL;

	of_node_put(np);

	if (!soc_data) {
		pr_err("%s: soc_data is NULL\n", __func__);
		return -EINVAL;
	}

	ret = mxs_suspend_alloc_ocram(*soc_data->suspend_asm_sz +
				      sizeof(*mxs_virt_addr),
				      &suspend_ocram_base);
	if (ret)
		return ret;

	memset(suspend_ocram_base, 0, sizeof(*mxs_virt_addr));
	mxs_virt_addr = suspend_ocram_base;

	get_virt_addr(soc_data->clkctrl_compat, &mxs_virt_addr->clkctrl_addr);
	get_virt_addr(soc_data->power_compat, &mxs_virt_addr->power_addr);
	get_virt_addr(soc_data->dram_compat, &mxs_virt_addr->dram_addr);
	get_virt_addr(soc_data->pinctrl_compat, &mxs_virt_addr->pinctrl_addr);
	get_virt_addr(soc_data->emi_compat, &mxs_virt_addr->emi_addr);
	get_virt_addr(soc_data->icoll_compat, &mxs_virt_addr->icoll_addr);
	get_virt_addr(soc_data->rtc_compat, &mxs_virt_addr->rtc_addr);

	cpu_clk = clk_get_sys("cpu", NULL);
	osc_clk = clk_get_sys("cpu_xtal", NULL);
	hbus_clk = clk_get_sys("hbus", NULL);

	if (IS_ERR(cpu_clk)) {
		pr_err("%s: failed to get cpu_clk with %ld\n",
		       __func__, PTR_ERR(cpu_clk));
		ret = -EIO;
		goto cpu_clk_err;
	}
	if (IS_ERR(osc_clk)) {
		pr_err("%s: failed to get osc_clk with %ld\n",
		       __func__, PTR_ERR(osc_clk));
		ret = -EIO;
		goto cpu_clk_err;
	}
	if (IS_ERR(hbus_clk)) {
		pr_err("%s: failed to get hbus_clk with %ld\n",
		       __func__, PTR_ERR(hbus_clk));
		ret = -EIO;
		goto cpu_clk_err;
	}

	suspend_asm = soc_data->suspend_asm;

	mxs_suspend_in_ocram_fn = fncpy(
		suspend_ocram_base + sizeof(*mxs_virt_addr),
		suspend_asm,
		*soc_data->suspend_asm_sz);

	suspend_set_ops(&mxs_suspend_ops);

	return 0;

cpu_clk_err:
	clk_put(hbus_clk);
	clk_put(osc_clk);
	clk_put(cpu_clk);

	return ret;
}

void __init mxs_pm_init(void)
{
	int ret;

	if (IS_ENABLED(CONFIG_SUSPEND)) {
		ret = mxs_suspend_init();
		if (ret)
			pr_warn("%s: No DDR LPM support with suspend %d!\n",
				__func__, ret);
	}

	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
}
