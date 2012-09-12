/*
 * arch/arm/mm/cache-l2x0.c - L210/L220 cache controller support
 *
 * Copyright (C) 2007 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/hardware/cache-l2x0.h>

#define CACHE_LINE_SIZE		32
#ifdef CONFIG_OPROFILE_ARM11_EVTMON
#include <linux/module.h>
#define L2_ENABLE_BIT           0x1
#define L2_EVTBUS_BIT           0x100000
#define L2_CTL_REG              (l2x0_base + L2X0_CTRL)
#define L2_AUX_REG              (l2x0_base + L2X0_AUX_CTRL)
#endif

static void __iomem *l2x0_base;
static unsigned long l2x0_aux;
static DEFINE_RAW_SPINLOCK(l2x0_lock);
bool l2x0_disabled;

static inline void sync_writel(unsigned long val, unsigned long reg,
			       unsigned long complete_mask)
{
	writel(val, l2x0_base + reg);
	/* wait for the operation to complete */
	while (readl(l2x0_base + reg) & complete_mask)
		;
}

static inline void cache_sync(void)
{
	sync_writel(0, L2X0_CACHE_SYNC, 1);
}

static inline void l2x0_inv_all(void)
{
	unsigned long flags;

	/* invalidate all ways */
	spin_lock_irqsave(&l2x0_lock, flags);
	sync_writel(0xff, L2X0_INV_WAY, 0xff);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_flush_all(void)
{
	/* clean and invalidate all ways */
	sync_writel(0xff, L2X0_CLEAN_INV_WAY, 0xff);
	cache_sync();
}

static void l2x0_inv_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
	if (start & (CACHE_LINE_SIZE - 1)) {
		start &= ~(CACHE_LINE_SIZE - 1);
		sync_writel(start, L2X0_CLEAN_INV_LINE_PA, 1);
		start += CACHE_LINE_SIZE;
	}

	if (end & (CACHE_LINE_SIZE - 1)) {
		end &= ~(CACHE_LINE_SIZE - 1);
		sync_writel(end, L2X0_CLEAN_INV_LINE_PA, 1);
	}

	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			sync_writel(start, L2X0_INV_LINE_PA, 1);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}

	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_clean_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			sync_writel(start, L2X0_CLEAN_LINE_PA, 1);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_flush_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			sync_writel(start, L2X0_CLEAN_INV_LINE_PA, 1);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

#ifdef CONFIG_OPROFILE_ARM11_EVTMON
/*!
 * Enable the EVTBUS to monitor L2 cache events
 */
void l2x0_evtbus_enable(void)
{
        unsigned int flags;

        local_irq_save(flags);
	/* If L2 cache is enabled then disable L2 cache, enable L2 evtbus,
	re-enable L2 cache */
        if ((readl(L2_CTL_REG) & L2_ENABLE_BIT) != 0) {
                writel(0, L2_CTL_REG);
                writel((readl(L2_AUX_REG)| L2_EVTBUS_BIT), L2_AUX_REG);
                writel(L2_ENABLE_BIT, L2_CTL_REG);
        } else {
                writel((readl(L2_AUX_REG)| L2_EVTBUS_BIT), L2_AUX_REG);
        }
        local_irq_restore(flags);
}

/*!
 * Disable the EVTBUS
 */
void l2x0_evtbus_disable(void)
{
        unsigned int flags;

        local_irq_save(flags);
	/* If L2 cache is enabled then disable L2 cache, disable L2 evtbus,
	re-enable L2 cache */
        if ((readl(L2_CTL_REG) & L2_ENABLE_BIT) != 0) {
                writel(0, L2_CTL_REG);
                writel((readl(L2_AUX_REG)& ~L2_EVTBUS_BIT), L2_AUX_REG);
                writel(L2_ENABLE_BIT, L2_CTL_REG);
        } else {
                writel((readl(L2_AUX_REG)& ~L2_EVTBUS_BIT), L2_AUX_REG);
        }
        local_irq_restore(flags);
}
EXPORT_SYMBOL(l2x0_evtbus_enable);
EXPORT_SYMBOL(l2x0_evtbus_disable);
#endif
void __init l2x0_init(void __iomem *base, __u32 aux_val, __u32 aux_mask)
{
	__u32 aux;

	if (l2x0_disabled) {
		printk(KERN_INFO "L2X0 cache controller disabled\n");
		return;
	}
	
	l2x0_base = base;

	if (!(readl(l2x0_base + L2X0_CTRL) & 1)) {
		/* l2x0 controller is disabled */
		aux = readl(l2x0_base + L2X0_AUX_CTRL);
		aux &= aux_mask;
		aux |= aux_val;
		writel(aux, l2x0_base + L2X0_AUX_CTRL);

		l2x0_inv_all();
		
		/* enable L2X0 */
		writel(1, l2x0_base + L2X0_CTRL);
	}

	outer_cache.inv_range = l2x0_inv_range;
	outer_cache.clean_range = l2x0_clean_range;
	outer_cache.flush_range = l2x0_flush_range;
	outer_cache.flush_all = l2x0_flush_all;

	printk(KERN_INFO "L2X0 cache controller enabled\n");
}
EXPORT_SYMBOL(outer_cache);

void l2x0_disable(void)
{
	if (readl(l2x0_base + L2X0_CTRL)
	    && !(readl(l2x0_base + L2X0_DEBUG_CTRL) & 0x2)) {
		l2x0_flush_all();
		writel(0, l2x0_base + L2X0_CTRL);
		l2x0_flush_all();
	}
}

void l2x0_enable(void)
{
	if (!readl(l2x0_base + L2X0_CTRL)) {
		writel(l2x0_aux, l2x0_base + L2X0_AUX_CTRL);
		l2x0_inv_all();
		/* enable L2X0 */
		writel(1, l2x0_base + L2X0_CTRL);
	}
}

static int __init l2x0_disable_cmdline(char *unused)
{
	l2x0_disabled = 1;
	return 0;
}
early_param("nol2x0", l2x0_disable_cmdline);
