/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>
#include "mxs-sleep.h"

#define HW_CLKCTRL_CPU	0x00000050

#define HW_DRAM_CTL06	0x00000018
#define HW_DRAM_CTL08	0x00000020

#define HW_EMI_STAT	0x00000010

/* Index on struct mxs_virt_addr_t */
#define CLKCTRL_IDX	0
#define POWER_IDX	1
#define DRAM_IDX	2
#define PINCTRL_IDX	3
#define EMI_IDX		4
.macro PM_GET_BASE_ADDR_OFF, n, offset, rr
        ldr r1, [sp, #4]
        ldr r0, [r1, #\n * 4]
        mov r1, #(\offset & 0x000000ff)
        orr r1, r1 , #(\offset & 0x0000ff00)
        orr r1, r1 , #(\offset & 0x00ff0000)
        orr r1, r1 , #(\offset & 0xff000000)
        add \rr, r0, r1
.endm

	.text

	.align 8
ENTRY(mx23_cpu_standby)
	/* save registers on stack */
	stmfd	sp!, {r0 - r9, lr}

	.space  0x100
__mx23_temp_stack:
        .space  128

	adr	r9, __mx23_temp_stack

	/* clean cache */
	ldr	r1, __mx23_flush_cache_addr
	mov	lr, pc
	mov	pc, r1

	/* put DRAM into self refresh */
	PM_GET_BASE_ADDR_OFF DRAM_IDX, HW_DRAM_CTL08, r0
	ldr	r1, [r0]
	orr	r1, r1, #(1 << 8)
	str	r1, [r0]

	/* wait for it to actually happen */
	PM_GET_BASE_ADDR_OFF EMI_IDX, HW_EMI_STAT, r0
1:	ldr	r1, [r0]
	teq	r1, #(1 << 1)
	beq	1b
	nop
	nop
	nop
	nop
	nop

	/* do enter standby */
	PM_GET_BASE_ADDR_OFF CLKCTRL_IDX, HW_CLKCTRL_CPU, r0
	mov	r1, #(1 << 12)
	str	r1, [r0, #4]
	mov	r2, #0
	mcr	p15, 0, r2, c7, c0, 4
	nop

	/* sleeping now... */

	/* remove INTERRUPT_WAIT bit */
	str	r1, [r0, #8]
	nop
	nop
	nop

	/* restore normal DRAM mode */
	PM_GET_BASE_ADDR_OFF DRAM_IDX, HW_DRAM_CTL08, r0
	ldr	r1, [r0]
	bic	r1, r1, #(1 << 8)
	str	r1, [r0]

	/* wait for it to actually happen */
	PM_GET_BASE_ADDR_OFF EMI_IDX, HW_EMI_STAT, r0
102:	ldr	r1, [r0]
	tst	r1, #(1 << 1)
	bne	102b

	nop
	nop
	nop

	/* restore regs and return */
	ldmfd   sp!, {r0 - r9, pc}

__mx23_flush_cache_addr:
	.word	arm926_flush_kern_cache_all

ENDPROC(mx23_cpu_standby)

ENTRY(mx23_cpu_standby_sz)
	.word	. - mx23_cpu_standby
