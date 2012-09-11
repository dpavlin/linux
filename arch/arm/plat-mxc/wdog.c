#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <asm/io.h>

#define WDOG_WCR	0x0	/* 16bit watchdog control reg */

#define WCR_SRS_BIT	(1 << 4)
#define WCR_WDE_BIT	(1 << 2)
#define WCR_WOE_BIT	(1 << 6)
#define WCR_WDA_BIT	(1 << 5)
#define WCR_SRS_BIT	(1 << 4)
#define WCR_WRE_BIT	(1 << 3)
#define WCR_WDBG_BIT	(1 << 1)
#define WCR_WDZST_BIT	(1 << 0)

#define WDOG_BASE_ADDR	IO_ADDRESS(WDOG1_BASE_ADDR)

#define WDOG_WSR	2	/* 16bit watchdog service reg */
#define WDOG_WRSR	4	/* 16bit watchdog reset status reg */

void mxc_wd_reset(void)
{
	u16 reg;
	struct clk *wdog_clk;

	wdog_clk = clk_get(NULL, "wdog_clk");
	clk_enable(wdog_clk);

	reg = __raw_readw(WDOG_BASE_ADDR + WDOG_WCR);
	reg = (reg & ~WCR_SRS_BIT) | WCR_WDE_BIT;
	__raw_writew(reg, WDOG_BASE_ADDR + WDOG_WCR);
}

static int g_wdog_count = 0;
/*!
 * Kick the watchdog from the timer interrupt
 */
#define MXC_WDOG_PING_THRESHOLD	100	/* Every 100 timer interrupts */

void mxc_kick_wd(void)
{
	if (g_wdog_count++ >= MXC_WDOG_PING_THRESHOLD) {
		g_wdog_count = 0;       /* reset */

		/* issue the service sequence instructions */
		__raw_writew(0x5555, WDOG_BASE_ADDR + WDOG_WSR);
		__raw_writew(0xAAAA, WDOG_BASE_ADDR + WDOG_WSR);
	}
}

/* Initialize the watchdog */
static int __init mxc_wd_init(void)
{
	u32 reg;
	unsigned short timeout = 0xff << 8;	/* Timeout of 127 seconds */

	printk(KERN_INFO "Initialize MXC Timer IRQ Watchdog\n");

	/* enable WD, suspend WD in DEBUG mode */
	reg = timeout | WCR_WOE_BIT | WCR_WDZST_BIT |
		WCR_SRS_BIT | WCR_WDA_BIT | WCR_WDE_BIT | WCR_WDBG_BIT;
	__raw_writew(reg, WDOG_BASE_ADDR + WDOG_WCR);

	/* Kick the wdog to get started */
	mxc_kick_wd();

	return 0;
}
postcore_initcall(mxc_wd_init);
