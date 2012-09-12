/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2010 Amazon Technologies Inc.
 * Manish Lachwani (lachwani@lab126.com)
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
 * @file plat-mxc/wdog.c
 * @brief This file contains watchdog timer implementations.
 *
 * This file contains watchdog timer implementations for timer tick.
 *
 * @ingroup WDOG
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <asm/io.h>

#define WDOG_WT                 0x8	/* WDOG WT starting bit inside WCR */
#define WCR_WOE_BIT             (1 << 6)
#define WCR_WDA_BIT             (1 << 5)
#define WCR_SRS_BIT             (1 << 4)
#define WCR_WRE_BIT             (1 << 3)
#define WCR_WDE_BIT             (1 << 2)
#define WCR_WDBG_BIT            (1 << 1)
#define WCR_WDZST_BIT           (1 << 0)

/*
 * WatchDog
 */
#define WDOG_WCR	0	/* 16bit watchdog control reg */
#define WDOG_WSR	2	/* 16bit watchdog service reg */
#define WDOG_WRSR	4	/* 16bit watchdog reset status reg */

/*
 * PROC entries
 */
#define PROC_WATCHDOG		"watchdog"
#define PROC_WATCHDOG_RESET	"reset"

static struct proc_dir_entry *proc_watchdog_parent;
static struct proc_dir_entry *proc_watchdog_reset;

/*!
 * The base addresses for the WDOG modules
 */
static unsigned long wdog_base[2] = {
	IO_ADDRESS(WDOG1_BASE_ADDR),
#ifdef WDOG2_BASE_ADDR
	IO_ADDRESS(WDOG2_BASE_ADDR),
#endif
};

extern void arch_reset(char mode);
extern void doze_disable(void);

void mxc_wd_reset(void)
{
	u16 reg;
	struct clk *clk;

	clk = clk_get(NULL, "wdog_clk");
	clk_enable(clk);
	reg = __raw_readw(wdog_base[0] + WDOG_WCR) & ~WCR_SRS_BIT;
	reg |= WCR_WDE_BIT;
	__raw_writew(reg, wdog_base[0] + WDOG_WCR);
}
EXPORT_SYMBOL(mxc_wd_reset);

static void mxc_reset(void)
{
	arch_reset((char)1);
}

static int g_wdog_count = 0;
#define WDOG_BASE_ADDR  IO_ADDRESS(WDOG1_BASE_ADDR)

/*!
 * Kick the watchdog from the timer interrupt
 */
#define MXC_WDOG_PING_THRESHOLD 100     /* Every 100 timer interrupts */

void mxc_kick_wd(void)
{
        if (g_wdog_count++ >= MXC_WDOG_PING_THRESHOLD) {
                g_wdog_count = 0;       /* reset */

                /* issue the service sequence instructions */
                __raw_writew(0x5555, WDOG_BASE_ADDR + WDOG_WSR);
                __raw_writew(0xAAAA, WDOG_BASE_ADDR + WDOG_WSR);
        }
}

/*!
 * Do a quick watchdog reset
 */
static int mxc_wdt_reset_write(struct file *file, const char __user *buf, 
				unsigned long count, void *data)
{
	char comm[TASK_COMM_LEN];

	mxc_reset();

	/* Point of no return */
	while (1) {
		/* Do nothing */
	}

	return -EINVAL;
}

static int __init mxc_watchdog_init(void)
{
	proc_watchdog_parent = create_proc_entry(PROC_WATCHDOG, S_IFDIR | S_IRUGO | S_IXUGO, NULL);
	if (proc_watchdog_parent != NULL) {
		proc_watchdog_reset = create_proc_entry(PROC_WATCHDOG_RESET,
						S_IWUSR | S_IRUGO, proc_watchdog_parent);
		if (proc_watchdog_reset != NULL) {
			proc_watchdog_reset->data = NULL;
			proc_watchdog_reset->read_proc = NULL;
			proc_watchdog_reset->write_proc = mxc_wdt_reset_write;
		}
	}
	return 0;
}

static void __exit mxc_watchdog_exit(void)
{
	if (proc_watchdog_parent != NULL) {
		remove_proc_entry(PROC_WATCHDOG_RESET, proc_watchdog_parent);
		remove_proc_entry(PROC_WATCHDOG, NULL);
		proc_watchdog_parent = proc_watchdog_reset = NULL;
	}
}

module_init(mxc_watchdog_init);
module_exit(mxc_watchdog_exit);

