#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <asm/io.h>

#define WDOG_WCR	0x0	/* 16bit watchdog control reg */
#define WCR_SRS_BIT	(1 << 4)
#define WCR_WDE_BIT	(1 << 2)
#define WDOG_BASE_ADDR	IO_ADDRESS(WDOG1_BASE_ADDR)

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

