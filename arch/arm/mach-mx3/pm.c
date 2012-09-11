/*
 * linux/arch/arm/mach-mx3/pm.c
 *
 * MX3 Power Management Routines
 *
 * Original code for the SA11x0:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Modified for the PXA250 by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * Modified for the OMAP1510 by David Singleton:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * Cleanup 2004 for OMAP1510/1610 by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * Copyright (c) 2008 lachwani@lab126.com Lab126, Inc.
 *
 * Modified for the MX31
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>
#include <linux/delay.h>
#ifdef CONFIG_MACH_MARIO_MX
#include <net/mwan.h>
#include <asm/arch/pmic_rtc.h>
#endif
#include <asm/io.h>
#include <asm/arch/mxc_pm.h>
#include <asm/arch/pmic_power.h>
#include <asm/arch/clock.h>

#define HAC_GRP  5
#define GEMK_GRP 5
#define GPIO_GRP 4
#define WDOG_GRP 4
#define GPT_GRP  2
#define UART_GRP 0

extern void clk_disable(struct clk *clk);
extern int clk_enable(struct clk *clk);
extern struct clk *clk_get(struct device *dev, const char *id);
#ifdef CONFIG_MACH_MARIO_MX
extern atomic_t lab126_mario_perf_log;
#endif

/*
 * TODO: whatta save?
 */

static int mx31_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_MEM:
		printk(KERN_INFO "State Retention Mode\n");
		mxc_pm_lowpower(STOP_MODE);
		break;

	case PM_SUSPEND_STANDBY:
		mxc_pm_lowpower(WAIT_MODE);
		break;

	case PM_SUSPEND_STOP:
		printk(KERN_INFO "Deep Sleep Mode\n");
		mxc_pm_lowpower(DSM_MODE);
		break;

	default:
		return -1;
	}
	return 0;
}

/*
 * Called after processes are frozen, but before we shut down devices.
 */
static int mx31_pm_prepare(suspend_state_t state)
{
#ifdef CONFIG_DSM
	t_pc_config pc_config;

	pc_config.auto_en_vbkup1 = 1;
	pc_config.auto_en_vbkup2= 1;
	pc_config.en_vbkup2= 1;
	pc_config.clk_32k_user_off = 0;
	pc_config.en_vbkup1 = 1;
	pc_config.vhold_voltage = 3;
	pc_config.vhold_voltage2 = 0;
	pc_config.warm_enable = 1;
	pc_config.pc_enable = 1;
	pc_config.pc_timer = 5;
	pc_config.pc_count_enable = 1;
	pc_config.pc_count = 0;
	pc_config.pc_max_count = 7;
	pc_config.user_off_pc = 1;
	pc_config.clk_32k_enable = 1;
	pmic_power_set_pc_config(&pc_config);
	mdelay(100);

	pmic_power_off();
#endif
	pmic_power_regulator_off(SW_PLL);

	/*
	 * Turn off Audio ciruitry and VAUDIO
	 */
	pmic_power_regulator_off(REGU_VVIB);
	pmic_power_regulator_off(REGU_VAUDIO);

	pmic_power_regulator_set_lp_mode(REGU_VDIG, LOW_POWER_CTRL_BY_PIN);
	pmic_power_regulator_set_lp_mode(REGU_GPO1, LOW_POWER_CTRL_BY_PIN);
	pmic_power_regulator_set_lp_mode(REGU_GPO2, LOW_POWER_CTRL_BY_PIN);
	pmic_power_regulator_set_lp_mode(REGU_GPO3, LOW_POWER_CTRL_BY_PIN);

#ifdef CONFIG_MACH_MARIO_MX
	/*
	 * Only when the WAN is off do we enable low power on GPO4 */
	if ( (wan_get_power_status() == WAN_OFF) ||
		(wan_get_power_status() == WAN_INVALID) )  {
			/* WAN is OFF */
			pmic_write_reg(REG_POWER_MISCELLANEOUS, (1 << 13), (1 << 13));
	}
	else {
			/* WAN is ON */ 
			pmic_write_reg(REG_POWER_MISCELLANEOUS, (0 << 13), (1 << 13));
	}
#endif

	pmic_power_regulator_set_lp_mode(REGU_VAUDIO, LOW_POWER_CTRL_BY_PIN);

	pmic_power_regulator_set_lp_mode(REGU_VIOHI, LOW_POWER_EN);
	pmic_power_regulator_set_lp_mode(REGU_VIOLO, LOW_POWER_EN);

	return 0;
}

/*
 * Called after devices are re-setup, but before processes are thawed.
 */
static int mx31_pm_finish(suspend_state_t state)
{
#ifdef CONFIG_MACH_MARIO_MX
	struct timeval tv;
	int print_perf = 0;

	if (unlikely(atomic_read(&lab126_mario_perf_log))) {
		pmic_rtc_get_time(&tv);
		print_perf = 1;
	}
#endif
	pmic_power_regulator_set_lp_mode(REGU_VIOLO, LOW_POWER_DISABLED);
	pmic_power_regulator_set_lp_mode(REGU_VIOHI, LOW_POWER_DISABLED);

	pmic_power_regulator_on(SW_PLL);
	pmic_power_regulator_on(REGU_VVIB);
	pmic_power_regulator_on(REGU_VAUDIO);

	mdelay(10);
	/* Turn off GPO4 */
	pmic_write_reg(REG_POWER_MISCELLANEOUS, (0 << 12), (1 << 12));
	mdelay(10);
	/* Turn on GPO4 */
	pmic_write_reg(REG_POWER_MISCELLANEOUS, (1 << 12), (1 << 12));
	mdelay(100);

#ifdef CONFIG_MACH_MARIO_MX
	if (unlikely(print_perf)) {
		printk(KERN_DEBUG "pm: P def:pmfinish:id=pmFinish,time=%lu000,type=absolute:received interrupt\n", tv.tv_sec);
	}
#endif
	return 0;
}

struct pm_ops mx31_pm_ops = {
	.prepare = mx31_pm_prepare,
	.enter = mx31_pm_enter,
	.finish = mx31_pm_finish,
	.valid = pm_valid_only_mem,
};

static int __init mx31_pm_init(void)
{
	printk(KERN_INFO "Power Management for Freescale MX31\n");
	pm_set_ops(&mx31_pm_ops);

	return 0;
}

late_initcall(mx31_pm_init);
