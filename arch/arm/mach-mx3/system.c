/*
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (c) 2008 lachwani@lab126.com Lab126, Inc.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/bootmem.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/proc-fns.h>
#include <asm/system.h>
#include <asm/arch/clock.h>
#include <net/mwan.h>
#include <asm/arch/pmic_power.h>
#ifdef CONFIG_MACH_MARIO_MX
#include <asm/arch/pmic_rtc.h>
#include <asm/arch/pmic_external.h>
#endif
#include "crm_regs.h"

/*!
 * @defgroup MSL_MX31 i.MX31 Machine Specific Layer (MSL)
 */

/*!
 * @file mach-mx3/system.c
 * @brief This file contains idle and reset functions.
 *
 * @ingroup MSL_MX31
 */

static int clks_initialized = 0;

static struct clk *sdma_clk, *mbx_clk, *ipu_clk, *mpeg_clk, *vpu_clk, *usb_clk,
    *rtic_clk, *nfc_clk, *emi_clk, *rng_clk, *sdma_ipg_clk,
	*cpu_clk, *ssi_clk, *rtc_clk, *wdog_clk, *iim_clk;

extern int mxc_jtag_enabled;

/*
 * Flag to control whether CPU should go into DOZE mode or WAIT mode
 */
extern atomic_t usb_dma_doze_ref_count;
volatile unsigned int reg, reg1, reg2, reg3;

/*
 * Track the number of times CPU gets into DOZE and WAIT
 */
int count_wait = 0, count_doze = 0, sdma_zero_count = 0, wan_count = 0, emi_zero_count = 0;

atomic_t doze_disable_cnt = ATOMIC_INIT(0);

void doze_enable(void)
{
	atomic_dec(&doze_disable_cnt);
	if (atomic_read(&doze_disable_cnt) < 0)
		atomic_set(&doze_disable_cnt, 0);
}

void doze_disable(void)
{
	atomic_inc(&doze_disable_cnt);
}

EXPORT_SYMBOL(doze_disable);
EXPORT_SYMBOL(doze_enable);


/*
 * sysfs interface to CPU idle counts
 */
static ssize_t
sysfs_show_idle_count(struct sys_device *dev, char *buf)
{
	char *curr = buf;

	curr += sprintf(curr, "WAIT MODE - %d\n", count_wait);
	curr += sprintf(curr, "DOZE MODE - %d\n", count_doze);
	curr += sprintf(curr, "SDMA ZERO COUNT - %d\n", sdma_zero_count);
	curr += sprintf(curr, "SDMA USECOUNT - %d\n", clk_get_usecount(sdma_clk));
	curr += sprintf(curr, "USB USECOUNT - %d\n", clk_get_usecount(usb_clk));
	curr += sprintf(curr, "EMI ZERO COUNT - %d\n", emi_zero_count);

	curr += sprintf(curr, "\n");
	return curr - buf;
}

/*
 * Sysfs setup bits:
 */
static SYSDEV_ATTR(idle_count, 0600, sysfs_show_idle_count, NULL);

static struct sysdev_class cpu_idle_sysclass = {
	set_kset_name("cpu_idle_count"),
};

static struct sys_device device_cpu_idle = {
	.id	= 0,
	.cls	= &cpu_idle_sysclass,
};

static int __init init_cpu_idle_sysfs(void)
{
	int error = sysdev_class_register(&cpu_idle_sysclass);

	if (!error)
		error = sysdev_register(&device_cpu_idle);
	if (!error)
		error = sysdev_create_file(
				&device_cpu_idle,
				&attr_idle_count);
	return error;
}

device_initcall(init_cpu_idle_sysfs);

/*!
 * This function puts the CPU into idle mode. It is called by default_idle()
 * in process.c file.
 */
extern void cpu_v6_doze_idle(void); // From /linux/arch/arm/mm/proc-v6.S.
void arch_idle(void)
{
	int emi_gated_off = 0;
	unsigned long ccmr;

	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks.
	 */
	if (!mxc_jtag_enabled) {
		if (clks_initialized == 0) {
			clks_initialized = 1;

			wdog_clk = clk_get(NULL, "wdog_clk");
			iim_clk = clk_get(NULL, "iim_clk");

			sdma_clk = clk_get(NULL, "sdma_ahb_clk");
			sdma_ipg_clk = clk_get(NULL, "sdma_ipg_clk");
			ipu_clk = clk_get(NULL, "ipu_clk");
			if (cpu_is_mx31()) {
				mpeg_clk = clk_get(NULL, "mpeg4_clk");
				mbx_clk = clk_get(NULL, "mbx_clk");
			} else {
				vpu_clk = clk_get(NULL, "vpu_clk");
			}
			rtc_clk = clk_get(NULL, "rtc_clk");
			usb_clk = clk_get(NULL, "usb_ahb_clk");
			rtic_clk = clk_get(NULL, "rtic_clk");
			nfc_clk = clk_get(NULL, "nfc_clk");
			emi_clk = clk_get(NULL, "emi_clk");
			cpu_clk = clk_get(NULL, "cpu_clk");
			ssi_clk = clk_get(NULL, "ssi_clk");

			clk_disable(nfc_clk);
		}

		if (clk_get_usecount(sdma_clk) == 0) 
			sdma_zero_count++;

		if  (atomic_read(&doze_disable_cnt) == 0) {
			count_doze++;

			ccmr = reg = __raw_readl(MXC_CCM_CCMR);
#ifdef CONFIG_MACH_MARIO_MX
			if ( (wan_get_power_status() == WAN_OFF) || 
				(wan_get_power_status() == WAN_INVALID) )  {
					reg &= 0xfffffcfe; /* UPLL, SPLL and FPME */
			}
			else
				reg &= 0xfffffefe; /* SPLL and FPME */
#endif
			__raw_writel(reg, MXC_CCM_CCMR);

			/*
			 * Clock Gating Registers 0-2
			 */
			reg1 = reg = __raw_readl(MXC_CCM_CGR0);
			reg &= 0xfff3ffff;
			__raw_writel(reg, MXC_CCM_CGR0);

			reg2 = reg = __raw_readl(MXC_CCM_CGR1);
			reg &= 0x00cc3300;
			__raw_writel(reg, MXC_CCM_CGR1);

			reg3 = reg = __raw_readl(MXC_CCM_CGR2);
			reg &= 0xffffc33c;
			__raw_writel(reg, MXC_CCM_CGR2);

			clk_disable(rng_clk); /* Security */
			clk_disable(rtc_clk);
			clk_disable(wdog_clk);
			clk_disable(iim_clk);
			clk_disable(ipu_clk);

			if ((clk_get_usecount(sdma_clk) == 0)
				&& (clk_get_usecount(ipu_clk) <= 1)
				&& (clk_get_usecount(usb_clk) == 0)
				&& (clk_get_usecount(rtic_clk) == 0)
				&& (clk_get_usecount(mpeg_clk) == 0)
				&& (clk_get_usecount(mbx_clk) == 0)
				&& (clk_get_usecount(nfc_clk) == 0)
				&& (clk_get_usecount(vpu_clk) == 0) ) {
					emi_zero_count++;
					emi_gated_off = 1;
					clk_disable(emi_clk);
			}

			cpu_do_idle();

			/*
			 * Restore CCMR
			 */
			__raw_writel(ccmr, MXC_CCM_CCMR);

			if (emi_gated_off == 1)
				clk_enable(emi_clk);	

			__raw_writel(reg1, MXC_CCM_CGR0);
			__raw_writel(reg2, MXC_CCM_CGR1);
			__raw_writel(reg3, MXC_CCM_CGR2);

			clk_enable(ipu_clk);
			clk_enable(wdog_clk);
			clk_enable(iim_clk);
			clk_enable(rng_clk);
			clk_enable(rtc_clk);
		} else {
			count_wait++;
			cpu_do_idle();
		}
	}
}

/*
 * mx31 reset Atlas but keep the backup regulators ON
 */
static void mx31_deep_reset(void)
{
	t_pc_config pc_config;
	unsigned int reg;

	pc_config.auto_en_vbkup2= 0;
	pc_config.en_vbkup2= 0;
	pc_config.vhold_voltage = 3;
	pc_config.vhold_voltage2 = 3;
	pc_config.auto_en_vbkup1 = 1;
	pc_config.en_vbkup1 = 0;
	pc_config.warm_enable = 0;
	pc_config.pc_enable = 1;
	pc_config.pc_timer = 3;
	pc_config.pc_count_enable = 1;
	pc_config.pc_count = 0;
	pc_config.pc_max_count = 7;
	pc_config.user_off_pc = 1;
	pmic_power_set_pc_config(&pc_config);
	udelay(1000);

	reg = __raw_readl(MXC_CCM_CCMR);
	reg = (reg & (~MXC_CCM_CCMR_LPM_MASK)) |
		2 << MXC_CCM_CCMR_LPM_OFFSET |
		MXC_CCM_CCMR_VSTBY;

	__raw_writel(reg, MXC_CCM_CCMR);
	pmic_power_off(); /* Trigger power off - starts 8ms timer */

	local_irq_disable();
	__asm__ __volatile__("mcr       p15, 0, r1, c7, c0, 4\n"
			     "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"::);

	while (1) {
		 /* do nothing */
	}
}

/*
 * This function resets the system. It is called by machine_restart().
 *
 * @param  mode	 indicates different kinds of resets
 */
void arch_reset(char mode)
{
#ifdef CONFIG_MACH_MARIO_MX
	struct timeval pmic_time;

	if (in_atomic())
		mxc_wd_reset();

	pmic_rtc_get_time(&pmic_time);
	pmic_time.tv_sec += 5;
	pmic_rtc_set_time_alarm(&pmic_time);
	
	//Clear TODAI interrupt flag
	pmic_write_reg(REG_INTERRUPT_STATUS_1, 0, 0x2);

	//Unmask TODAM interrupt
	pmic_write_reg(REG_INTERRUPT_MASK_1, 0, 0x2);

	mx31_deep_reset();
#else
	/* Assert SRS signal */
	mxc_wd_reset();
#endif
}

#ifdef CONFIG_MACH_LAB126
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/syscalls.h>
EXPORT_SYMBOL(sys_write);

#include <asm/arch/boot_globals.h>

void *oops_start;
int __init mxc_check_oops(void)
{
	unsigned long oops_ddr_start = OOPS_SAVE_BASE;
	char oops_buffer[1024];
	oops_start = __arm_ioremap(oops_ddr_start, OOPS_SAVE_SIZE, 0);

	memcpy((void *)oops_buffer, oops_start, 1023);

	if ( (oops_buffer[0] == 'O') && (oops_buffer[1] == 'O') &&
		(oops_buffer[2] == 'P') && (oops_buffer[3] == 'S') ) {
			printk(KERN_ERR "boot: I def:oops::Kernel Crash Start\n");
			printk ("%s", oops_buffer);
			memcpy((void *)oops_buffer, (oops_start + 1024), 1023);
			printk ("%s", oops_buffer);
			memcpy((void *)oops_buffer, (oops_start + 2048), 1023);
			printk ("%s", oops_buffer);
			memcpy((void *)oops_buffer, (oops_start + 3072), 1023);
			printk ("%s", oops_buffer);
			printk ("\nboot: I def:oops::Kernel Crash End\n");
	}

	memset(oops_start, 0, OOPS_SAVE_SIZE);
	return 1;
}
late_initcall(mxc_check_oops);

#endif
