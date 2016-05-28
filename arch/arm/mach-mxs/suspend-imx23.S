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

/* CLKCTRL registers */
#define HW_CLKCTRL_CPU				0x00000050
#define HW_CLKCTRL_HBUS				0x00000030
#define HW_CLKCTRL_FRAC				0x000000f0
#define HW_CLKCTRL_EMI				0x000000a0
#define HW_CLKCTRL_PLLCTRL0			0x00000000
#define BM_CLKCTRL_EMI_CLKGATE			0x80000000
#define BM_CLKCTRL_FRAC_CLKGATEEMI		0x00008000
#define BM_CLKCTRL_FRAC_CLKGATECPU		0x00000080
#define BM_CLKCTRL_PLLCTRL0_POWER		0x00010000

/* DRAM control registers */
#define HW_DRAM_CTL16				0x00000040
#define BP_DRAM_CTL16_LOWPOWER_CONTROL		16
#define BM_DRAM_CTL16_LOWPOWER_CONTROL		0x001F0000
#define BF_DRAM_CTL16_LOWPOWER_CONTROL(v) \
        (((v) << 16) & BM_DRAM_CTL16_LOWPOWER_CONTROL)

#define BP_DRAM_CTL16_LOWPOWER_AUTO_ENABLE	8
#define BM_DRAM_CTL16_LOWPOWER_AUTO_ENABLE	0x00001F00
#define BF_DRAM_CTL16_LOWPOWER_AUTO_ENABLE(v)  \
		(((v) << 8) & BM_DRAM_CTL16_LOWPOWER_AUTO_ENABLE)

#define HW_PINCTRL_DOUT0_TOG			0x0000050C
#define HW_PINCTRL_DOUT2_TOG			0x0000052C

.macro PM_BITS_SET, val
        ldr	r1, [r0]
        orr	r1, r1, #(\val)
        str r1, [r0]
.endm

.macro PM_BITS_CLR, val
        ldr	r1, [r0]
        bic	r1, r1, #(\val)
        str r1, [r0]
.endm

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

/* this toggles bank 0, gpio 16 */
.macro TOGGLE_GPIO
        ldr r1, [r0]
        orr r1, r1, #(0x10000)
        str r1, [r0]
	nop
.endm

/* this toggles green led on bank 2, gpio 1 */
.macro TOGGLE_LED
	ldr r1, [r0]
        orr r1, r1, #(0x02)
        str r1, [r0]
.endm

@ Usage
#    @gpio toggle
#    PM_GET_BASE_ADDR_OFF PINCTRL_IDX, HW_PINCTRL_DOUT0_TOG, r0
#    TOGGLE_GPIO

	.text

	.align 8
ENTRY(mx23_cpu_standby)
	/* save registers on stack */
	stmfd	sp!, {r0 - r9, lr}

	.space  0x100
__mx23_temp_stack:
        .space  128

	adrl	r9, __mx23_temp_stack

	PM_GET_BASE_ADDR_OFF PINCTRL_IDX, HW_PINCTRL_DOUT2_TOG, r0
	TOGGLE_LED

	/* clean cache */
	ldr	r1, __mx23_flush_cache_addr
	mov	lr, pc
	mov	pc, r1

	/* put DRAM into self refresh */
	PM_GET_BASE_ADDR_OFF DRAM_IDX, HW_DRAM_CTL16, r0
        ldr     r1,[r0]
        and	r1, r1, #(~BM_DRAM_CTL16_LOWPOWER_AUTO_ENABLE)
        orr	r1, r1, #(BF_DRAM_CTL16_LOWPOWER_AUTO_ENABLE(2))
        str	r1, [r0]
        ldr     r1,[r0]
        and	r1, r1, #(~BM_DRAM_CTL16_LOWPOWER_CONTROL)
        orr	r1, r1, #(BF_DRAM_CTL16_LOWPOWER_CONTROL(2))
        str	r1, [r0]

        /* wait for it to actually happen */
        mov	r0, #24 << 12
11:	sub	r0, r0, #1
        cmp	r0, #0
        bne	11b

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
	PM_GET_BASE_ADDR_OFF DRAM_IDX, HW_DRAM_CTL16, r0
	ldr	r1, [r0]
	bic	r1, r1, #(BF_DRAM_CTL16_LOWPOWER_CONTROL(2))
	str	r1, [r0]
	ldr	r1, [r0]
	bic	r1, r1, #(BF_DRAM_CTL16_LOWPOWER_AUTO_ENABLE(2))
	str	r1, [r0]

	/* wait for it to actually happen */
	mov	r0, #24 << 12
12:	sub	r0, r0, #1
	cmp	r0, #0
	bne	12b

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
