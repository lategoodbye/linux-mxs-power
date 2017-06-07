/*
 * ARC HSDK Platform support code
 *
 * Copyright (C) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/arcregs.h>
#include <asm/io.h>
#include <asm/mach_desc.h>

/*
 * By default ICCM is mapped to 0x7z while this area is used for
 * Virtual kernel mappings, so move it to currently unused area.
 */
static void __init relocate_iccm(void)
{
	if (cpuinfo_arc700[smp_processor_id()].iccm.sz)
		write_aux_reg(ARC_REG_AUX_ICCM, 0x60000000);
}

/*
 * Default configuration of PAE regs doesn't work for us causing
 * problems with DMA to/from peripherals even if PAE40 is not used.
 */
static void __init fixup_pae_regs(void)
{
#define ARC_PERIPHERAL_BASE	0xf0000000
#define	CREG_BASE		(ARC_PERIPHERAL_BASE + 0x1000)
#define	CREG_PAE		(CREG_BASE + 0x180)
#define	CREG_PAE_UPDATE		(CREG_BASE + 0x194)

	/* Default is 1, which means "PAE offset = 4GByte" */
	writel_relaxed(0, (void __iomem *) CREG_PAE);

	/* Really apply settings made above */
	writel(1, (void __iomem *) CREG_PAE_UPDATE);
}

static void __init hsdk_early_init(void)
{
	fixup_pae_regs();
}

static void __init hsdk_init_per_cpu(unsigned int cpu)
{
	relocate_iccm();
}

static const char *hsdk_compat[] __initconst = {
	"snps,hsdk",
	NULL,
};

MACHINE_START(SIMULATION, "hsdk")
	.dt_compat	= hsdk_compat,
	.init_early	= hsdk_early_init,
	.init_per_cpu	= hsdk_init_per_cpu,
MACHINE_END
