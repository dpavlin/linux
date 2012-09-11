/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (c) 2008 lachwani@lab126.com Lab126, Inc.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file dvfs_v2.c
 *
 * @brief A simplied driver for the Freescale Semiconductor MXC DVFS module.
 *
 * Upon initialization, the DVFS driver initializes the DVFS hardware
 * sets up driver nodes attaches to the DVFS interrupt and initializes internal
 * data structures. When the DVFS interrupt occurs the driver checks the cause
 * of the interrupt (lower frequency, increase frequency or emergency) and changes
 * the CPU voltage according to translation table that is loaded into the driver.
 *
 * @ingroup PM
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/pm.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>

#include <asm/arch/gpio.h>
#include <asm/arch/pmic_external.h>
#include <asm/arch/pmic_power.h>
#include <asm/arch/clock.h>

#include <asm/arch/mxc_pm.h>

#include "iomux.h"
#include "crm_regs.h"

static int dvfs_is_active;

/* Used for tracking the number of interrupts */
static u32 dvfs_nr_up[4];
static u32 dvfs_nr_dn[4];

/*
 * Clock structures 
 */
static struct clk *cpu_clk;
static struct clk *ahb_clk;

enum {
	FSVAI_FREQ_NOCHANGE = 0x0,
	FSVAI_FREQ_INCREASE,
	FSVAI_FREQ_DECREASE,
	FSVAI_FREQ_EMERG,
};

/*
 * Frequency increase threshold. Increase frequency change request
 * will be sent if DVFS counter value will be more than this value.
 */
#define DVFS_UPTHR		(30 << MXC_CCM_LTR0_UPTHR_OFFSET)

/*
 * Frequency decrease threshold. Decrease frequency change request
 * will be sent if DVFS counter value will be less than this value.
 */
#define DVFS_DNTHR		(18 << MXC_CCM_LTR0_DNTHR_OFFSET)

/*
 * With the ARM clocked at 532, this setting yields a DIV_3_CLK of 2.03 kHz.
 */
#define DVFS_DIV3CK		(3 << MXC_CCM_LTR0_DIV3CK_OFFSET)

/*
 * DNCNT defines the amount of times the down threshold should be exceeded
 * before DVFS will trigger frequency decrease request.
 */
#define DVFS_DNCNT		(0x33 << MXC_CCM_LTR1_DNCNT_OFFSET)

/*
 * UPCNT defines the amount of times the up threshold should be exceeded
 * before DVFS will trigger frequency increase request.
 */
#define DVFS_UPCNT		(0x33 << MXC_CCM_LTR1_UPCNT_OFFSET)

/*
 * Panic threshold. Panic frequency change request
 * will be sent if DVFS counter value will be more than this value.
 */
#define DVFS_PNCTHR		(63 << MXC_CCM_LTR1_PNCTHR_OFFSET)

/*
 * Load tracking buffer source: 1 for ld_add; 0 for pre_ld_add
 */
#define DVFS_LTBRSR		(1 << MXC_CCM_LTR1_LTBRSR_OFFSET)

/* EMAC defines how many samples are included in EMA calculation */
#define DVFS_EMAC		(0x20 << MXC_CCM_LTR2_EMAC_OFFSET)

const static u8 ltr_gp_weight[] = {
	0,			/* 0 */
	0,
	0,
	0,
	0,
	0,			/* 5 */
	0,
	0,
	0,
	0,
	0,			/* 10 */
	0,
	7,
	7,
	7,
	7,			/* 15 */
};

DEFINE_SPINLOCK(mxc_dvfs_lock);

/*!
 * This function sets the weight of general purpose signals
 * @param   gp_id   number of general purpose bit
 * @param   weight  the weight of the general purpose bit
 */
static void set_gp_weight(int gp_id, u8 weight)
{
	u32 reg;

	if (gp_id < 9) {
		reg = __raw_readl(MXC_CCM_LTR3);
		reg = (reg & ~(MXC_CCM_LTR3_WSW_MASK(gp_id))) |
		    (weight << MXC_CCM_LTR3_WSW_OFFSET(gp_id));
		__raw_writel(reg, MXC_CCM_LTR3);
	} else if (gp_id < 16) {
		reg = __raw_readl(MXC_CCM_LTR2);
		reg = (reg & ~(MXC_CCM_LTR2_WSW_MASK(gp_id))) |
		    (weight << MXC_CCM_LTR2_WSW_OFFSET(gp_id));
		__raw_writel(reg, MXC_CCM_LTR2);
	}
}

static int start_dvfs(void)
{
	u32 reg, flags;

	if (dvfs_is_active) {
		return 0;
	}

	spin_lock_irqsave(&mxc_dvfs_lock, flags);

	reg = __raw_readl(MXC_CCM_PMCR0);

	/* enable dvfs and interrupt */
	reg = (reg & ~MXC_CCM_PMCR0_FSVAIM) | MXC_CCM_PMCR0_DVFEN;

	__raw_writel(reg, MXC_CCM_PMCR0);

	dvfs_is_active = 1;

	spin_unlock_irqrestore(&mxc_dvfs_lock, flags);

	pr_info("DVFS is started\n");

	return 0;
}

#define MXC_CCM_LTR0_CONFIG_MASK	(MXC_CCM_LTR0_UPTHR_MASK | \
					 MXC_CCM_LTR0_DNTHR_MASK | \
					 MXC_CCM_LTR0_DIV3CK_MASK)
#define MXC_CCM_LTR0_CONFIG_VAL		(DVFS_UPTHR | DVFS_DNTHR | DVFS_DIV3CK)

#define MXC_CCM_LTR1_CONFIG_MASK	(MXC_CCM_LTR1_UPCNT_MASK | \
					 MXC_CCM_LTR1_DNCNT_MASK | \
					 MXC_CCM_LTR1_PNCTHR_MASK | \
					 MXC_CCM_LTR1_LTBRSR_MASK)
#define MXC_CCM_LTR1_CONFIG_VAL		(DVFS_UPCNT | DVFS_DNCNT | \
					 DVFS_PNCTHR | DVFS_LTBRSR)

/*!
 * This function is called for module initialization.
 * It sets up the DVFS hardware.
 * It sets default values for DVFS thresholds and counters. The default
 * values was chosen from a set of different reasonable values. They was tested
 * and the default values in the driver gave the best results.
 * More work should be done to find optimal values.
 *
 * @return   0 if successful; non-zero otherwise.
 *
 */
static int init_dvfs_controller(void)
{
	u32 i, reg;

	/* Configure 2 MC13783 DVFS pins */
	mxc_request_iomux(MX31_PIN_DVFS0, OUTPUTCONFIG_FUNC, INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_DVFS1, OUTPUTCONFIG_FUNC, INPUTCONFIG_NONE);

	/* Configure MC13783 voltage ready input pin */
	mxc_request_iomux(MX31_PIN_GPIO1_5, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_FUNC);

	/* setup LTR0 */
	reg = __raw_readl(MXC_CCM_LTR0);
	reg = (reg & ~(MXC_CCM_LTR0_CONFIG_MASK)) | MXC_CCM_LTR0_CONFIG_VAL;
	__raw_writel(reg, MXC_CCM_LTR0);

	/* set up LTR1 */
	reg = __raw_readl(MXC_CCM_LTR1);
	reg = (reg & ~(MXC_CCM_LTR1_CONFIG_MASK)) | MXC_CCM_LTR1_CONFIG_VAL;
	__raw_writel(reg, MXC_CCM_LTR1);

	/* setup LTR2 */
	reg = __raw_readl(MXC_CCM_LTR2);
	reg = (reg & ~(MXC_CCM_LTR2_EMAC_MASK)) | DVFS_EMAC;
	__raw_writel(reg, MXC_CCM_LTR2);

	/* Set general purpose weights to 0 */
	for (i = 0; i < 16; i++) {
		set_gp_weight(i, ltr_gp_weight[i]);
	}

	/* ARM interrupt, mask load buf full interrupt */
	reg = __raw_readl(MXC_CCM_PMCR0);
	reg |= MXC_CCM_PMCR0_DVFIS | MXC_CCM_PMCR0_LBMI;
	__raw_writel(reg, MXC_CCM_PMCR0);

	return 0;
}

static irqreturn_t dvfs_irq(int irq, void *dev_id)
{
	u32 pmcr0 = __raw_readl(MXC_CCM_PMCR0);
	u32 fsvai = (pmcr0 & MXC_CCM_PMCR0_FSVAI_MASK) >>
	    MXC_CCM_PMCR0_FSVAI_OFFSET;
	u32 dvsup = (pmcr0 & MXC_CCM_PMCR0_DVSUP_MASK) >>
	    MXC_CCM_PMCR0_DVSUP_OFFSET;
	u32 curr_ahb, curr_cpu, rate;

	/* Should not be here if FSVAIM is set */
	BUG_ON(pmcr0 & MXC_CCM_PMCR0_FSVAIM);

	if (fsvai == FSVAI_FREQ_NOCHANGE) {
		/* Do nothing. Freq change is not required */
		printk(KERN_WARNING "fsvai should not be 0\n");
		return IRQ_HANDLED;
	}

	if (!(pmcr0 & MXC_CCM_PMCR0_UPDTEN)) {
		/* Do nothing. DVFS didn't finish previous flow update */
		return IRQ_HANDLED;
	}

	if (((dvsup == DVSUP_LOW) && (fsvai == FSVAI_FREQ_DECREASE)) ||
	    ((dvsup == DVSUP_TURBO) && ((fsvai == FSVAI_FREQ_INCREASE) ||
					(fsvai == FSVAI_FREQ_EMERG)))) {
		/* Interrupt should be disabled in these cases according to
		 * the spec since DVFS is already at lowest (highest) state */
		printk(KERN_WARNING "Something is wrong?\n");
		return IRQ_HANDLED;
	}

	curr_ahb = clk_get_rate(ahb_clk);
	if (fsvai == FSVAI_FREQ_DECREASE) {
		curr_cpu = clk_get_rate(cpu_clk);
		rate = ((curr_cpu / curr_ahb) - 1) * curr_ahb;
		if ((cpu_is_mx31_rev(CHIP_REV_2_0) < 0) &&
		    ((curr_cpu / curr_ahb) == 4)) {
			rate = ((curr_cpu / curr_ahb) - 2) * curr_ahb;
		}
		dvfs_nr_dn[dvsup]++;
	} else {
		rate = 4 * curr_ahb;
		dvfs_nr_up[dvsup]++;
	}

	clk_set_rate(cpu_clk, rate);
	return IRQ_HANDLED;
}

/*!
 * This function disables the DVFS module.
 */
static void stop_dvfs(void)
{
	u32 pmcr0, dvsup, flags;
	u32 curr_ahb = clk_get_rate(ahb_clk);

	if (dvfs_is_active) {
		spin_lock_irqsave(&mxc_dvfs_lock, flags);

		pmcr0 = __raw_readl(MXC_CCM_PMCR0);
		dvsup = (pmcr0 & MXC_CCM_PMCR0_DVSUP_MASK) >>
		    MXC_CCM_PMCR0_DVSUP_OFFSET;
		if (dvsup != DVSUP_TURBO) {
			/* Use sw delay to insure volt/freq change */
			clk_set_rate(cpu_clk, (4 * curr_ahb));
			udelay(200);
		}

		pmcr0 = __raw_readl(MXC_CCM_PMCR0);
		/* disable dvfs and its interrupt */
		pmcr0 = (pmcr0 & ~MXC_CCM_PMCR0_DVFEN) | MXC_CCM_PMCR0_FSVAIM;
		__raw_writel(pmcr0, MXC_CCM_PMCR0);

		dvfs_is_active = 0;

		spin_unlock_irqrestore(&mxc_dvfs_lock, flags);
	}

	pr_info("DVFS is stopped\n");
}

void pmic_voltage_init(void)
{
	t_regulator_voltage volt;
	unsigned int reg;

	reg = __raw_readl(MXC_CCM_RCSR);
	printk(KERN_ERR "RCSR register - %x\n", reg);

	/* Enable 4 mc13783 output voltages */
	pmic_write_reg(REG_ARBITRATION_SWITCHERS, (1 << 5), (1 << 5));

	/* Enable mc13783 voltage ready signal */
	pmic_write_reg(REG_INTERRUPT_MASK_1, (0 << 11), (1 << 11));

	/* Set mc13783 DVS speed 25mV each 4us */
	pmic_write_reg(REG_SWITCHERS_4, (1 << 6), (3 << 6));
	pmic_write_reg(REG_SWITCHERS_4, (1 << 14), (3 << 14));

	/*
	 * Set DVS on SW1A and SW1B
	 */
	volt.sw1a = SW1A_1_625V;
	pmic_power_regulator_set_voltage(SW_SW1A, volt);
	
	volt.sw1a = SW1A_1_275V;
	pmic_power_switcher_set_dvs(SW_SW1A, volt);

	volt.sw1b = SW1B_1_25V;
	pmic_power_switcher_set_dvs(SW_SW1B, volt);

	/*
	 * standby voltages on all switchers and the power mode
	 */
	volt.sw1a = SW1A_0_95V;
	pmic_power_switcher_set_stby(SW_SW1A, volt);
	pmic_power_switcher_set_mode(SW_SW1A, LOW_POWER, 1);

	pmic_power_switcher_set_mode(SW_SW1B, LOW_POWER, 1);
	pmic_power_switcher_set_mode(SW_SW2A, LOW_POWER, 1);

	volt.sw2b = SW2B_1_7V;
	pmic_power_switcher_set_stby(SW_SW2B, volt);
	pmic_power_switcher_set_mode(SW_SW2B, LOW_POWER, 1);

	/*
	 * run mode on all switchers
	 *
	 * SW1A and SW1B - processor
	 * SW2A - Broadsheet SDR
	 * SW2B - DDR
	 */
	pmic_power_switcher_set_mode(SW_SW1A, PULSE_SKIP, 0);
	pmic_power_switcher_set_panic_mode(SW_SW1A, 1);

	pmic_power_switcher_set_mode(SW_SW1B, PULSE_SKIP, 0);
	pmic_power_switcher_set_panic_mode(SW_SW1B, 1);

	pmic_power_switcher_set_mode(SW_SW2A, PULSE_SKIP, 0);
	pmic_power_switcher_set_panic_mode(SW_SW2A, 1);

	pmic_power_switcher_set_mode(SW_SW2B, PULSE_SKIP, 0);
	pmic_power_switcher_set_panic_mode(SW_SW2B, 1);

#ifdef CONFIG_MACH_MARIO_MX
	pmic_power_regulator_off(SW_SW3);
	pmic_power_regulator_off(REGU_VGEN);
	pmic_power_regulator_off(REGU_VSIM);
	pmic_power_regulator_off(REGU_VESIM);
	pmic_power_regulator_off(REGU_VCAM);
	pmic_power_regulator_off(REGU_VRFBG);
	pmic_power_regulator_off(REGU_VRF1);
	pmic_power_regulator_off(REGU_VRF2);
	pmic_power_regulator_off(REGU_VRFCP);
	pmic_power_regulator_off(REGU_VRFREF);
	pmic_power_regulator_off(REGU_VRFDIG);

	pmic_power_regulator_off(REGU_GPO2); /* Nothing */
#endif
	/*
	 * Unsubscribe to the PWRRDYI event
	 */
	pmic_write_reg(REG_INTERRUPT_MASK_1, (1 << 11), (1 << 11));
}

/*
 * CLKO enable/disable for diags
 */

static int cko_en = 0; /* Enabled by default */

static ssize_t cko_ckoen_store(struct sys_device *dev, const char *buf,
				size_t size)
{
	struct clk *cko_clk = clk_get(NULL, "cko1_clk");

	if (strstr(buf, "1") != NULL) {
		clk_enable(cko_clk);
		cko_en = 1;
	}
	else if (strstr(buf, "0") != NULL) {
		clk_disable(cko_clk);
		cko_en = 0;
	}

	return size;
}

static ssize_t cko_ckoen_show(struct sys_device *dev, char *buf)
{
	int size = 0;

	if (cko_en == 0)
		size = sprintf(buf, "CLKO is disabled\n");
	else
		size = sprintf(buf, "CLKO is enabled\n");

	return size;
}

static SYSDEV_ATTR(ckoen, 0644, cko_ckoen_show, cko_ckoen_store);

static struct sysdev_class cko_sysclass = {
	set_kset_name("cko"),
};

static struct sys_device cko_device = {
	.id = 0,
	.cls = &cko_sysclass,
};

static int cko_sysdev_ctrl_init(void)
{
	int err;

	err = sysdev_class_register(&cko_sysclass);
	if (!err)
		err = sysdev_register(&cko_device);
	if (!err)
		err = sysdev_create_file(&cko_device, &attr_ckoen);

	return err;
}

static void cko_sysdev_ctrl_exit(void)
{
	sysdev_remove_file(&cko_device, &attr_ckoen);
	sysdev_unregister(&cko_device);
	sysdev_class_unregister(&cko_sysclass);
}

static ssize_t dvfs_enable_store(struct sys_device *dev, const char *buf,
				 size_t size)
{
	if (strstr(buf, "1") != NULL) {
		if (start_dvfs() != 0) {
			printk(KERN_ERR "Failed to start DVFS\n");
		}
	} else if (strstr(buf, "0") != NULL) {
		stop_dvfs();
	}

	return size;
}

static ssize_t dvfs_status_show(struct sys_device *dev, char *buf)
{
	int size = 0;

	if (dvfs_is_active) {
		size = sprintf(buf, "DVFS is enabled\n");
	} else {
		size = sprintf(buf, "DVFS is disabled\n");
	}
	size +=
	    sprintf((buf + size), "UP:\t%d\t%d\t%d\t%d\n", dvfs_nr_up[0],
		    dvfs_nr_up[1], dvfs_nr_up[2], dvfs_nr_up[3]);
	size +=
	    sprintf((buf + size), "DOWN:\t%d\t%d\t%d\t%d\n\n", dvfs_nr_dn[0],
		    dvfs_nr_dn[1], dvfs_nr_dn[2], dvfs_nr_dn[3]);

	return size;
}

static ssize_t dvfs_status_store(struct sys_device *dev, const char *buf,
				 size_t size)
{
	if (strstr(buf, "reset") != NULL) {
		int i;
		for (i = 0; i < 4; i++) {
			dvfs_nr_up[i] = 0;
			dvfs_nr_dn[i] = 0;
		}
	}

	return size;
}

static SYSDEV_ATTR(enable, 0200, NULL, dvfs_enable_store);
static SYSDEV_ATTR(status, 0644, dvfs_status_show, dvfs_status_store);

static struct sysdev_class dvfs_sysclass = {
        //.resume = NULL,
	set_kset_name("dvfs"),
};

static struct sys_device dvfs_device = {
	.id = 0,
	.cls = &dvfs_sysclass,
};

static int dvfs_sysdev_ctrl_init(void)
{
	int err;

	err = sysdev_class_register(&dvfs_sysclass);
	if (!err)
		err = sysdev_register(&dvfs_device);
	if (!err) {
		err = sysdev_create_file(&dvfs_device, &attr_enable);
		err = sysdev_create_file(&dvfs_device, &attr_status);
	}

	return err;
}

static void dvfs_sysdev_ctrl_exit(void)
{
	sysdev_remove_file(&dvfs_device, &attr_enable);
	sysdev_remove_file(&dvfs_device, &attr_status);
	sysdev_unregister(&dvfs_device);
	sysdev_class_unregister(&dvfs_sysclass);
}

static int __init dvfs_init(void)
{
	int err = 0;
	pmic_voltage_init();

	cpu_clk = clk_get(NULL, "cpu_clk");
	ahb_clk = clk_get(NULL, "ahb_clk");
	err = init_dvfs_controller();
	if (err) {
		printk(KERN_ERR "DVFS: Unable to initialize DVFS");
		return err;
	}

	/* request the DVFS interrupt */
	err = request_irq(INT_DVFS, dvfs_irq, IRQF_DISABLED, "dvfs", NULL);
	if (err) {
		printk(KERN_ERR "DVFS: Unable to attach to DVFS interrupt");
	}

	err = dvfs_sysdev_ctrl_init();
	if (err) {
		printk(KERN_ERR
		       "DVFS: Unable to register sysdev entry for dvfs");
		return err;
	}

	err = cko_sysdev_ctrl_init();
	if (err) {
		printk(KERN_ERR
			"CKO: Unable to register sysdev entry for CLKO");
		return err;
	}

	return err;
}

static void __exit dvfs_cleanup(void)
{
	stop_dvfs();

	/* release the DVFS interrupt */
	free_irq(INT_DVFS, NULL);

	dvfs_sysdev_ctrl_exit();
	cko_sysdev_ctrl_exit();

	clk_put(cpu_clk);
	clk_put(ahb_clk);
}

module_init(dvfs_init);
module_exit(dvfs_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("DVFS driver");
MODULE_LICENSE("GPL");
