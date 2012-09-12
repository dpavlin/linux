/*
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Amazon Technologies, Inc. All Rights Reserved.
 * Author: Manish Lachwani (lachwani@lab126.com)
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
 * @file mx_sdhci.c
 *
 * @brief Driver for the Freescale Semiconductor MXC eSDHC modules.
 *
 * This driver code is based on sdhci.c, by Pierre Ossman <drzeus@drzeus.cx>");
 * This driver supports Enhanced Secure Digital Host Controller
 * modules eSDHC of MXC. eSDHC is also referred as enhanced MMC/SD
 * controller.
 *
 * @ingroup MMC_SD
 */

/*
 * On the Luigi board, the following is the MMC/SD layout:
 *
 * MMC Slot #1 - eMMC
 * MMC Slot #2 - Atheros WiFi Ar6002
 * MMC Slot #3 - SD card 
 */

#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include <linux/leds.h>
#include <linux/workqueue.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>
#include <linux/clk.h>
#include <linux/regulator/regulator.h>
#include <linux/pmic_external.h>
#include <linux/mmc/sdio.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>
#include <asm/arch/mmc.h>
#include <asm/arch/board_id.h>

#include "mx_sdhci.h"

#define DRIVER_NAME "mxsdhci"

#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__, ## x)

static unsigned int debug_quirks;
static int last_op_dir;

/*
 * Different quirks to handle when the hardware deviates from a strict
 * interpretation of the SDHCI specification.
 */

/* Controller doesn't honor resets unless we touch the clock register */
#define SDHCI_QUIRK_CLOCK_BEFORE_RESET			(1<<0)
/* Controller has bad caps bits, but really supports DMA */
#define SDHCI_QUIRK_FORCE_DMA				(1<<1)
/* Controller doesn't like to be reset when there is no card inserted. */
#define SDHCI_QUIRK_NO_CARD_NO_RESET			(1<<2)
/* Controller doesn't like clearing the power reg before a change */
#define SDHCI_QUIRK_SINGLE_POWER_WRITE			(1<<3)
/* Controller has flaky internal state so reset it on each ios change */
#define SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS		(1<<4)
/* Controller has an unusable DMA engine */
#define SDHCI_QUIRK_BROKEN_DMA				(1<<5)
/* Controller can only DMA from 32-bit aligned addresses */
#define SDHCI_QUIRK_32BIT_DMA_ADDR			(1<<6)
/* Controller can only DMA chunk sizes that are a multiple of 32 bits */
#define SDHCI_QUIRK_32BIT_DMA_SIZE			(1<<7)
/* Controller needs to be reset after each request to stay stable */
#define SDHCI_QUIRK_RESET_AFTER_REQUEST			(1<<8)
/* Controller needs voltage and power writes to happen separately */
#define SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER		(1<<9)
/* Controller has an off-by-one issue with timeout value */
#define SDHCI_QUIRK_INCR_TIMEOUT_CONTROL		(1<<10)
/* Controller only support the PIO */
#define SDHCI_QUIRK_ONLY_PIO 				(1<<16)
/* Controller support the External DMA */
#define SDHCI_QUIRK_EXTERNAL_DMA_MODE			(1<<17)
/* Controller support the Internal Simple DMA */
#define SDHCI_QUIRK_INTERNAL_SIMPLE_DMA			(1<<18)
/* Controller support the Internal Advanced DMA */
#define SDHCI_QUIRK_INTERNAL_ADVANCED_DMA 		(1<<19)

/* Check if SDIO clock is in use  - 20 seconds*/
#define SDIO_CLK_RESUME_THRESHOLD			20000

/*
 * defines the mxc flags refer to the special hw pre-conditons and behavior
 */
static unsigned int mxc_quirks;
#ifdef CONFIG_MMC_IMX_ESDHCI_PIO_MODE
static unsigned int debug_quirks = SDHCI_QUIRK_ONLY_PIO;
#else
static unsigned int debug_quirks;
#endif
static unsigned int mxc_wml_value = 512;
static unsigned int *adma_des_table;

static void sdhci_prepare_data(struct sdhci_host *, struct mmc_data *);
static void sdhci_finish_data(struct sdhci_host *);

static void sdhci_send_command(struct sdhci_host *, struct mmc_command *);
static void sdhci_finish_command(struct sdhci_host *);

/* Used to active the SD bus */
extern void gpio_sdhc_active(int module);
extern void gpio_sdhc_inactive(int module);
static void sdhci_dma_irq(void *devid, int error, unsigned int cnt);

int sd_turn_of_dma = 0;
EXPORT_SYMBOL(sd_turn_of_dma);

static int sd1_turn_of_dma = 0;
static int sd2_turn_of_dma = 0;

/* Keep track of small transfers */
static int sdhci_pio_counter = 0;

static struct regulator *vsd_reg;

struct sdhci_host *sdio_host;

/* Always follow the driver's idle pm model */
static int sdio_mmc1_clock = 1;
extern int sdio_reset(struct mmc_host *host);
extern int mmc_go_idle(struct mmc_host *host);
atomic_t sdio_removed = ATOMIC_INIT(0);
EXPORT_SYMBOL(sdio_removed);

extern int mmc_sdio_reinit_card(struct mmc_host *host, u32 ocr);

void sdhci_reset_sdio(void)
{
	mmc_claim_host(sdio_host->mmc);
	mmc_go_idle(sdio_host->mmc);
	mdelay(20);
	mmc_sdio_reinit_card(sdio_host->mmc, sdio_host->mmc->ocr);
	mmc_release_host(sdio_host->mmc);
}
EXPORT_SYMBOL(sdhci_reset_sdio);

/* Gate the mmc1 clock and set the variables correctly */
static void sdio_mmc1_clk_work(struct work_struct *work)
{
	/* WiFi module is now gone */
	sdio_mmc1_clock = 1;
	clk_disable(sdio_host->clk);
	sdio_host->plat_data->clk_flg = 0;
}

static void sdio_mmc1_resume_work(struct work_struct *work)
{
	if (sdio_mmc1_clock) {
		if (sdio_host->plat_data->clk_flg) {
			clk_disable(sdio_host->clk);
			sdio_host->plat_data->clk_flg = 0;
		}
	}
}

static DECLARE_DELAYED_WORK(sdio_mmc1_clk_wq, sdio_mmc1_clk_work);
static DECLARE_DELAYED_WORK(sdio_mmc1_resume_wq, sdio_mmc1_resume_work);

static int sdio_lpm_threshold_level = 1000;	/* After 1s */

DEFINE_SPINLOCK(sdio_lpm_mutex);

static int sdio_do_not_gate_clk = 0;

static void sdio_lpm_func(struct work_struct *unused);
DECLARE_DELAYED_WORK(sdio_lpm_work, sdio_lpm_func);

static void sdhci_idle_bus_adjust(struct sdhci_host *host, u8 idle);

void sdhci_touch_mmc1_clk(int enable)
{
	if (enable) {
		sdio_mmc1_clock = 0;
		sdio_do_not_gate_clk = 0;
		if (!sdio_host->plat_data->clk_flg) {
			clk_enable(sdio_host->clk);
			sdio_host->plat_data->clk_flg = 1;
		}
	}
	else {
		sdio_mmc1_clk_work((void *)0);
	}
}
EXPORT_SYMBOL(sdhci_touch_mmc1_clk);

void sdhci_sdio_stop_gating(void)
{
	sdhci_idle_bus_adjust(sdio_host, 0);
	sdio_do_not_gate_clk = 1;
}
EXPORT_SYMBOL(sdhci_sdio_stop_gating);

static int cardint_sdio_irq = 0;
static int cardint_stat = 0;

static int sdio_lpm_counter = 0;

static void sdhci_sdio_clk(int enable);

static int sdio_lpm_enabled = 0;

static void sdio_lpm_func(struct work_struct *unused)
{
	u32 ctrl = 0;

	if (sdio_lpm_enabled || sdio_do_not_gate_clk)
		return;

	disable_irq(sdio_host->irq);
	spin_lock(&sdio_lpm_mutex);

	ctrl = readl(sdio_host->ioaddr + SDHCI_HOST_CONTROL);
	if (!(ctrl & SDHCI_CTRL_4BITBUS)) {
		/* Card is already in 1-bit mode, time to gate clock and disable IRQ */
		sdio_lpm_counter++;
		sdhci_sdio_clk(0);
		sdio_lpm_enabled = 1;
	}

	spin_unlock(&sdio_lpm_mutex);
	enable_irq(sdio_host->irq);
}

static void sdhci_dumpregs(struct sdhci_host *host)
{
	printk(KERN_ERR DRIVER_NAME
	       ": ============== REGISTER DUMP ==============\n");

	printk(KERN_ERR DRIVER_NAME ": Sys addr: 0x%08x | Version:  0x%08x\n",
	       readl(host->ioaddr + SDHCI_DMA_ADDRESS),
	       readl(host->ioaddr + SDHCI_HOST_VERSION));
	printk(KERN_ERR DRIVER_NAME ": Blk size: 0x%08x | Blk cnt:  0x%08x\n",
	       (readl(host->ioaddr + SDHCI_BLOCK_SIZE) & 0xFFFF),
	       (readl(host->ioaddr + SDHCI_BLOCK_COUNT) >> 16));
	printk(KERN_ERR DRIVER_NAME ": Argument: 0x%08x | Trn mode: 0x%08x\n",
	       readl(host->ioaddr + SDHCI_ARGUMENT),
	       readl(host->ioaddr + SDHCI_TRANSFER_MODE));
	printk(KERN_ERR DRIVER_NAME ": Present:  0x%08x | Host ctl: 0x%08x\n",
	       readl(host->ioaddr + SDHCI_PRESENT_STATE),
	       readl(host->ioaddr + SDHCI_HOST_CONTROL));
	printk(KERN_ERR DRIVER_NAME ": Clock:    0x%08x\n",
	       readl(host->ioaddr + SDHCI_CLOCK_CONTROL));
	printk(KERN_ERR DRIVER_NAME ": Int stat: 0x%08x\n",
	       readl(host->ioaddr + SDHCI_INT_STATUS));
	printk(KERN_ERR DRIVER_NAME ": Int enab: 0x%08x | Sig enab: 0x%08x\n",
	       readl(host->ioaddr + SDHCI_INT_ENABLE),
	       readl(host->ioaddr + SDHCI_SIGNAL_ENABLE));
	printk(KERN_ERR DRIVER_NAME ": Caps:     0x%08x\n",
	       readl(host->ioaddr + SDHCI_CAPABILITIES));
	printk(KERN_ERR DRIVER_NAME ": Present:  0x%08x\n", 
		readl(host->ioaddr + SDHCI_PRESENT_STATE));

	printk(KERN_ERR DRIVER_NAME "cardint_sdio_irq=%d, cardint_stat=%d\n",
				cardint_sdio_irq, cardint_stat);

	printk(KERN_ERR DRIVER_NAME "sd1_turn_of_dma=%d, sd2_turn_of_dma=%d\n",
				sd1_turn_of_dma, sd2_turn_of_dma);

	printk(KERN_ERR DRIVER_NAME "SDIO LPM Counter=%d\n",sdio_lpm_counter);

	printk(KERN_ERR DRIVER_NAME
	       ": ===========================================\n");
}

static int sdio_debug_level = 0;

static ssize_t sdhci_registers_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	sdhci_dumpregs(sdio_host);		
	return sprintf(buf, "0\n");
}

static DEVICE_ATTR(sdhci_registers, 0644, sdhci_registers_show, NULL);

static ssize_t sdio_pio_transfers_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", sdhci_pio_counter);
}
static DEVICE_ATTR(sdio_pio_transfers, 0644, sdio_pio_transfers_show, NULL);

static ssize_t sdio_lpm_idle_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", sdio_lpm_counter);
}
static DEVICE_ATTR(sdio_lpm_idle, 0644, sdio_lpm_idle_show, NULL);

static ssize_t sdio_debug_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", sdio_debug_level);
}

static ssize_t sdio_debug_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error reading debug_level\n");	
		return -EINVAL;
	}

	if (value >= 0) {
		sdio_debug_level = value;
	}

	return size;
}

static DEVICE_ATTR(sdio_debug, 0644, sdio_debug_show, sdio_debug_store);

static ssize_t sdio_lpm_threshold_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", sdio_lpm_threshold_level);
}

static ssize_t sdio_lpm_threshold_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		printk(KERN_ERR "Error reading lpm threshold level\n");	
		return -EINVAL;
	}

	if (value > 0) {
		sdio_lpm_threshold_level = value;
	}

	return size;
}
static DEVICE_ATTR(sdio_lpm_threshold, 0644, sdio_lpm_threshold_show, sdio_lpm_threshold_store);

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static void sdhci_reset(struct sdhci_host *host, u8 mask)
{
	unsigned long tmp;
	unsigned long mask_u32;
	unsigned long reg_save = 0;

	if (host->chip->quirks & SDHCI_QUIRK_NO_CARD_NO_RESET) {
		if (!(readl(host->ioaddr + SDHCI_PRESENT_STATE) &
		      SDHCI_CARD_PRESENT))
			return;
	}

	if (mask & SDHCI_RESET_ALL)
		host->clock = 0;
	else if (host->flags & SDHCI_CD_PRESENT)
		reg_save = readl(host->ioaddr + SDHCI_HOST_CONTROL);

	tmp = readl(host->ioaddr + SDHCI_CLOCK_CONTROL) | (mask << 24);
	mask_u32 = readl(host->ioaddr + SDHCI_SIGNAL_ENABLE);
	writel(tmp, host->ioaddr + SDHCI_CLOCK_CONTROL);

	/* Wait max 100 ms */
	tmp = 5000;

	/* hw clears the bit when it's done */
	while ((readl(host->ioaddr + SDHCI_CLOCK_CONTROL) >> 24) & mask) {
		if (tmp == 0) {
			printk(KERN_ERR "%s: Reset 0x%x never completed.\n",
			       mmc_hostname(host->mmc), (int)mask);
			sdhci_dumpregs(host);
			return;
		}
		tmp--;
		udelay(20);
	}
	/*
	 * The INT_EN SIG_EN regs have been modified after reset.
	 * re-configure them ag.
	 */
	if (!(mask & SDHCI_RESET_ALL) && (host->flags & SDHCI_CD_PRESENT))
		writel(reg_save, host->ioaddr + SDHCI_HOST_CONTROL);
	if (host->flags & SDHCI_USE_DMA)
		mask_u32 &= ~(SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL);
	if (mxc_wml_value == 512)
		writel(SDHCI_WML_128_WORDS, host->ioaddr + SDHCI_WML);
	else
		writel(SDHCI_WML_16_WORDS, host->ioaddr + SDHCI_WML);
	writel(mask_u32, host->ioaddr + SDHCI_INT_ENABLE);
	writel(mask_u32, host->ioaddr + SDHCI_SIGNAL_ENABLE);
	last_op_dir = 0;
}

static void sdhci_init(struct sdhci_host *host)
{
	u32 intmask;

	sdhci_reset(host, SDHCI_RESET_ALL);

	intmask = SDHCI_INT_ADMA_ERROR | SDHCI_INT_ACMD12ERR |
	    SDHCI_INT_DATA_END_BIT | SDHCI_INT_DATA_CRC |
	    SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX |
	    SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT |
	    SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL |
	    SDHCI_INT_DMA_END | SDHCI_INT_DATA_END | SDHCI_INT_RESPONSE;

	if (host->flags & SDHCI_USE_DMA)
		intmask &= ~(SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL);
	/* Configure the WML rege */
	if (mxc_wml_value == 512)
		writel(SDHCI_WML_128_WORDS, host->ioaddr + SDHCI_WML);
	else
		writel(SDHCI_WML_16_WORDS, host->ioaddr + SDHCI_WML);
	writel(intmask, host->ioaddr + SDHCI_INT_ENABLE);
	writel(intmask, host->ioaddr + SDHCI_SIGNAL_ENABLE);
	mdelay(10);
}

static void sdhci_activate_led(struct sdhci_host *host)
{
	u32 ctrl;

	ctrl = readl(host->ioaddr + SDHCI_HOST_CONTROL);
	ctrl |= SDHCI_CTRL_LED;
	writel(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);
}

static void sdhci_deactivate_led(struct sdhci_host *host)
{	
	u32 ctrl;
	
	ctrl = readl(host->ioaddr + SDHCI_HOST_CONTROL);
	ctrl &= ~SDHCI_CTRL_LED;
	writel(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);
}

static void sdhci_sdio_clk(int enable)
{
	/* No WiFi? */
	if (sdio_mmc1_clock)
		return;

        if (enable) {
		/* Enable the clock */
                if (!sdio_host->plat_data->clk_flg) {
			sdio_host->plat_data->clk_flg = 1;
                        clk_enable(sdio_host->clk);
                }
        }
        else {
		/* Gate the sdio clock */
		if (sdio_host->plat_data->clk_flg) {
			clk_disable(sdio_host->clk);
			sdio_host->plat_data->clk_flg = 0;
                }
        }
}

/*
 * Handle bus case where controller cannot detect CIRQ reliably when in 4-bit mode.
 * Only valid for SDIO since MMC supports 8-bit width
 */
static void sdhci_idle_bus_adjust(struct sdhci_host *host, u8 idle)
{
	u32 ctrl;

	if (sdio_do_not_gate_clk)
		return;

	if (host->flags & SDHCI_IN_4BIT_MODE) {
		/* while bus is idle, leave it in 1-bit mode at the controller level */
		sdhci_sdio_clk(1);
		ctrl = readl(host->ioaddr + SDHCI_HOST_CONTROL);
		ctrl &= ~SDHCI_CTRL_4BITBUS; 
		if (!idle) {
			ctrl |= SDHCI_CTRL_4BITBUS; 
			sdio_lpm_enabled = 0;
		}
		writel(ctrl, host->ioaddr + SDHCI_HOST_CONTROL); 
		if (idle) {
			schedule_delayed_work(&sdio_lpm_work,
				msecs_to_jiffies(sdio_lpm_threshold_level));
		}
	}
}

/*****************************************************************************\
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
\*****************************************************************************/

static inline char *sdhci_sg_to_buffer(struct sdhci_host *host)
{
	return sg_virt(host->cur_sg);
}

static inline int sdhci_next_sg(struct sdhci_host *host)
{
	/*
	 * Skip to next SG entry.
	 */
	host->cur_sg++;
	host->num_sg--;

	/*
	 * Any entries left?
	 */
	if (host->num_sg > 0) {
		host->offset = 0;
		host->remain = host->cur_sg->length;
	}

	return host->num_sg;
}

static void sdhci_read_block_pio(struct sdhci_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int size;

	DBG("PIO reading\n");

	blksize = host->data->blksz;
	chunk_remain = 0;
	data = 0;

	buffer = sdhci_sg_to_buffer(host) + host->offset;

	while (blksize) {
		if (chunk_remain == 0) {
			data = readl(host->ioaddr + SDHCI_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			*buffer = data & 0xFF;
			buffer++;
			data >>= 8;
			size--;
		}

		if (host->remain == 0) {
			if (sdhci_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = sdhci_sg_to_buffer(host);
		}
	}
}

static void sdhci_write_block_pio(struct sdhci_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int bytes, size;

	DBG("PIO writing\n");

	blksize = host->data->blksz;
	chunk_remain = 4;
	data = 0;

	bytes = 0;
	buffer = sdhci_sg_to_buffer(host) + host->offset;

	while (blksize) {
		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			data >>= 8;
			data |= (u32) *buffer << 24;
			buffer++;
			size--;
		}

		if (chunk_remain == 0) {
			writel(data, host->ioaddr + SDHCI_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		if (host->remain == 0) {
			if (sdhci_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = sdhci_sg_to_buffer(host);
		}
	}
}

static void sdhci_transfer_pio(struct sdhci_host *host)
{
	u32 mask;

	BUG_ON(!host->data);

	if (host->num_sg == 0)
		return;

	if (host->data->flags & MMC_DATA_READ)
		mask = SDHCI_DATA_AVAILABLE;
	else
		mask = SDHCI_SPACE_AVAILABLE;

	while (readl(host->ioaddr + SDHCI_PRESENT_STATE) & mask) {
		if (host->data->flags & MMC_DATA_READ)
			sdhci_read_block_pio(host);
		else
			sdhci_write_block_pio(host);

		if (host->num_sg == 0)
			break;
	}

	DBG("PIO transfer complete.\n");
}

static void sdhci_prepare_data(struct sdhci_host *host, struct mmc_data *data)
{
	u32 count;
	unsigned target_timeout, current_timeout;

	WARN_ON(host->data);

	if (data == NULL)
		return;

	/* Sanity checks */
	BUG_ON(data->blksz * data->blocks > 524288);
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > 65535);

	host->data = data;
	host->data_early = 0;

	if (host->data->flags & MMC_DATA_READ)
		writel(readl(host->ioaddr + SDHCI_CLOCK_CONTROL) |
			SDHCI_CLOCK_HLK_EN,
			host->ioaddr + SDHCI_CLOCK_CONTROL);

	/* timeout in us */
	target_timeout = data->timeout_ns / 1000 +
	    data->timeout_clks / host->clock;

	/*
	 * Figure out needed cycles.
	 * We do this in steps in order to fit inside a 32 bit int.
	 * The first step is the minimum timeout, which will have a
	 * minimum resolution of 6 bits:
	 * (1) 2^13*1000 > 2^22,
	 * (2) host->timeout_clk < 2^16
	 *     =>
	 *     (1) / (2) > 2^6
	 */
	count = 0;
	current_timeout = (1 << 13) * 1000 / host->timeout_clk;
	while (current_timeout < target_timeout) {
		count++;
		current_timeout <<= 1;
		if (count >= 0xF)
			break;
	}

	/*
	 * Compensate for an off-by-one error in the CaFe hardware; otherwise,
	 * a too-small count gives us interrupt timeouts.
	 */
	if ((host->chip->quirks & SDHCI_QUIRK_INCR_TIMEOUT_CONTROL))
		count++;

	if (count >= 0xF) {
		DBG("%s: Too large timeout requested!\n",
		    mmc_hostname(host->mmc));
		count = 0xE;
	}

	/* Set the max time-out value to level up the compatibility */
	count = 0xE;

	count =
	    (count << 16) | (readl(host->ioaddr + SDHCI_CLOCK_CONTROL) &
			     0xFFF0FFFF);
	writel(count, host->ioaddr + SDHCI_CLOCK_CONTROL);

	if (host->flags & SDHCI_USE_DMA)
		host->flags |= SDHCI_REQ_USE_DMA;

	if (unlikely((host->flags & SDHCI_REQ_USE_DMA) &&
		     (host->chip->quirks & SDHCI_QUIRK_32BIT_DMA_SIZE) &&
		     ((data->blksz * data->blocks) & 0x3))) {
		printk("Reverting to PIO because of transfer size (%d)\n",
		    data->blksz * data->blocks);
		host->flags &= ~SDHCI_REQ_USE_DMA;
	}

	/*
	 * The assumption here being that alignment is the same after
	 * translation to device address space.
	 */
	if (unlikely((host->flags & SDHCI_REQ_USE_DMA) &&
		     (host->chip->quirks & SDHCI_QUIRK_32BIT_DMA_ADDR) &&
		     (data->sg->offset & 0x3))) {
		printk("Reverting to PIO because of bad alignment\n");
		host->flags &= ~SDHCI_REQ_USE_DMA;
	}

	if (((data->blksz * data->blocks) <= 128) && (host->id == 1) ) {
		host->flags &= ~SDHCI_REQ_USE_DMA;
		DBG("Reverting to PIO in small data transfer.\n");
		writel(readl(host->ioaddr + SDHCI_INT_ENABLE)
			| SDHCI_INT_DATA_AVAIL
			| SDHCI_INT_SPACE_AVAIL,
			host->ioaddr + SDHCI_INT_ENABLE);
		writel(readl(host->ioaddr + SDHCI_SIGNAL_ENABLE)
			| SDHCI_INT_DATA_AVAIL
			| SDHCI_INT_SPACE_AVAIL,
			host->ioaddr + SDHCI_SIGNAL_ENABLE);

		if (host->id == 1)
			sdhci_pio_counter++;
	} else if (host->flags & SDHCI_USE_DMA) {
		host->flags |= SDHCI_REQ_USE_DMA;
		DBG("Reverting to DMA in large data transfer.\n");
		writel(readl(host->ioaddr + SDHCI_INT_ENABLE)
			& ~(SDHCI_INT_DATA_AVAIL
			| SDHCI_INT_SPACE_AVAIL),
			host->ioaddr + SDHCI_INT_ENABLE);
		writel(readl(host->ioaddr + SDHCI_SIGNAL_ENABLE)
			& ~(SDHCI_INT_DATA_AVAIL
			| SDHCI_INT_SPACE_AVAIL),
			host->ioaddr + SDHCI_SIGNAL_ENABLE);
	}

	if (host->flags & SDHCI_REQ_USE_DMA) {
		int i;
		struct scatterlist *tsg;

		DBG("Configure the sg DMA, %s, len is 0x%x\n",
		    (data->flags & MMC_DATA_READ)
		    ? "DMA_FROM_DEIVCE" : "DMA_TO_DEVICE", data->sg_len);
		count =
		    dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			       (data->
				flags & MMC_DATA_READ) ? DMA_FROM_DEVICE :
			       DMA_TO_DEVICE);
		BUG_ON(count != data->sg_len);

		/* Make sure the ADMA mode is selected. */
		i = readl(host->ioaddr + SDHCI_HOST_CONTROL);
		i |= SDHCI_CTRL_ADMA;
		writel(i, host->ioaddr + SDHCI_HOST_CONTROL);

		tsg = data->sg;
		/* ADMA mode is used, create the descriptor table */
		for (i = 0; i < count; i++) {
			if (tsg->dma_address & 0xFFF) {
				DBG("ADMA addr isn't 4K aligned.\n");
				DBG("0x%x\n", tsg->dma_address);
				DBG("Changed to Single DMA mode %d\n", count);
				goto Single_DMA;
			}

			adma_des_table[2 * i] = tsg->length << 12;
			adma_des_table[2 * i] |= FSL_ADMA_DES_ATTR_SET;
			adma_des_table[2 * i] |= FSL_ADMA_DES_ATTR_VALID;
			adma_des_table[2 * i + 1] = tsg->dma_address;
			adma_des_table[2 * i + 1] |= FSL_ADMA_DES_ATTR_TRAN;
			adma_des_table[2 * i + 1] |= FSL_ADMA_DES_ATTR_VALID;
			if (count == (i + 1))
				adma_des_table[2 * i + 1] |=
				    FSL_ADMA_DES_ATTR_END;
			tsg++;
		}

		/* Write the physical address to ADMA address reg */
		writel(virt_to_phys(adma_des_table),
		       host->ioaddr + SDHCI_ADMA_ADDRESS);
	      Single_DMA:
		/* Rollback to the Single DMA mode */
		i = readl(host->ioaddr + SDHCI_HOST_CONTROL);
		i &= ~SDHCI_CTRL_ADMA;
		writel(i, host->ioaddr + SDHCI_HOST_CONTROL);
		/* Single DMA mode is used */
		writel(sg_dma_address(data->sg),
		       host->ioaddr + SDHCI_DMA_ADDRESS);
	} else if ((host->flags & SDHCI_USE_EXTERNAL_DMA) &&
		   (data->blocks * data->blksz >= mxc_wml_value)) {
		host->dma_size = data->blocks * data->blksz;
		printk("Configure the External DMA, %s, len is 0x%x\n",
		    (data->flags & MMC_DATA_READ)
		    ? "DMA_FROM_DEIVCE" : "DMA_TO_DEVICE", host->dma_size);

		if (data->blksz & 0x3) {
			printk(KERN_ERR
			       "mxc_mci: block size not multiple of 4 bytes\n");
		}

		if (data->flags & MMC_DATA_READ)
			host->dma_dir = DMA_FROM_DEVICE;
		else
			host->dma_dir = DMA_TO_DEVICE;

		host->dma_len = dma_map_sg(mmc_dev(host->mmc), data->sg,
					   data->sg_len, host->dma_dir);

		if (data->flags & MMC_DATA_READ) {
			mxc_dma_sg_config(host->dma, data->sg, data->sg_len,
					  host->dma_size, MXC_DMA_MODE_READ);
		} else {
			mxc_dma_sg_config(host->dma, data->sg, data->sg_len,
					  host->dma_size, MXC_DMA_MODE_WRITE);
		}
	} else {
		host->cur_sg = data->sg;
		host->num_sg = data->sg_len;

		host->offset = 0;
		host->remain = host->cur_sg->length;
	}

	/* We do not handle DMA boundaries, so set it to max (512 KiB) */
	writel((data->blocks << 16) | SDHCI_MAKE_BLKSZ(7, data->blksz),
	       host->ioaddr + SDHCI_BLOCK_SIZE);
}

static void sdhci_finish_data(struct sdhci_host *host)
{
	struct mmc_data *data;
	u16 blocks;

	BUG_ON(!host->data);

	data = host->data;
	host->data = NULL;

	if (host->flags & SDHCI_REQ_USE_DMA) {
		dma_unmap_sg(&(host->chip->pdev)->dev, data->sg, data->sg_len,
			     (data->flags & MMC_DATA_READ) ? DMA_FROM_DEVICE :
			     DMA_TO_DEVICE);
	}
	if ((host->flags & SDHCI_USE_EXTERNAL_DMA) &&
	    (host->dma_size >= mxc_wml_value)) {
		dma_unmap_sg(mmc_dev(host->mmc), data->sg,
			     host->dma_len, host->dma_dir);
		host->dma_size = 0;
	}

	/*
	 * Controller doesn't count down when in single block mode.
	 */
	if (data->blocks == 1)
		blocks = (data->error == 0) ? 0 : 1;
	else
		blocks = readl(host->ioaddr + SDHCI_BLOCK_COUNT) >> 16;
	data->bytes_xfered = data->blksz * data->blocks;

	if (host->id == 0)
		sd1_turn_of_dma--;

	if (host->id == 1)
		sd2_turn_of_dma--;

	sd_turn_of_dma = sd2_turn_of_dma | sd1_turn_of_dma;

	if (data->stop) {
		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */
		if (data->error) {
			sdhci_reset(host, SDHCI_RESET_CMD);
			sdhci_reset(host, SDHCI_RESET_DATA);
		}
		sdhci_send_command(host, data->stop);
	} else
		tasklet_schedule(&host->finish_tasklet);
}

static void sdhci_send_command(struct sdhci_host *host, struct mmc_command *cmd)
{
	int flags, tmp=0;
	u32 mask;
	u32 mode = 0;
	unsigned long timeout;

	if ( (sdio_debug_level > 0) && (host->id == 1)) {
		printk("sdhci_send_command is starting...\n");
	}

	WARN_ON(host->cmd);

	mask = SDHCI_CMD_INHIBIT;
	if ((cmd->data != NULL) || (cmd->flags & MMC_RSP_BUSY))
		mask |= SDHCI_DATA_INHIBIT;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (host->mrq->data && (cmd == host->mrq->data->stop))
		mask &= ~SDHCI_DATA_INHIBIT;

	if (host->id == 1)
		sdhci_idle_bus_adjust(host, 0);

	/* Wait max 10 ms */
	timeout = (10*256) + 255;

	while (readl(host->ioaddr + SDHCI_PRESENT_STATE) & mask) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Controller never released "
			       "inhibit bit(s).\n", mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			cmd->error = -EIO;
			tasklet_schedule(&host->finish_tasklet);
			return;
		}
		timeout--;
		if (!(timeout & 0xFF)) 
			mdelay(1);
	}

	mod_timer(&host->timer, jiffies + 10 * HZ);

	host->cmd = cmd;

	if (cmd->data != NULL) {
		if (host->id == 0)
			sd1_turn_of_dma++;

		if (host->id == 1)
			sd2_turn_of_dma++;

		sd_turn_of_dma = sd2_turn_of_dma | sd1_turn_of_dma;
	}

	sdhci_prepare_data(host, cmd->data);

	writel(cmd->arg, host->ioaddr + SDHCI_ARGUMENT);

	/* Set up the transfer mode */
	if (cmd->data != NULL) {
		mode = SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_DPSEL;
		if (cmd->data->blocks > 1) {
			mode |= SDHCI_TRNS_MULTI;
			tmp = readl(host->ioaddr + SDHCI_INT_ENABLE);
			tmp &= ~SDHCI_INT_ACMD12ERR;
			writel(tmp, host->ioaddr + SDHCI_INT_ENABLE);
		}

		if (cmd->data->flags & MMC_DATA_READ)
			mode |= SDHCI_TRNS_READ;
		else
			mode &= ~SDHCI_TRNS_READ;
		if (host->flags & SDHCI_REQ_USE_DMA)
			mode |= SDHCI_TRNS_DMA;
		if (host->flags & SDHCI_USE_EXTERNAL_DMA)
			printk("Prepare data completely in %s transfer mode.\n",
			    "EXTTERNAL DMA");
	}

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		printk(KERN_ERR "%s: Unsupported response type!\n",
		       mmc_hostname(host->mmc));
		cmd->error = -EINVAL;
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (!(cmd->flags & MMC_RSP_PRESENT))
		flags = SDHCI_CMD_RESP_NONE;
	else if (cmd->flags & MMC_RSP_136)
		flags = SDHCI_CMD_RESP_LONG;
	else if (cmd->flags & MMC_RSP_BUSY)
		flags = SDHCI_CMD_RESP_SHORT_BUSY;
	else
		flags = SDHCI_CMD_RESP_SHORT;

	if (cmd->flags & MMC_RSP_CRC)
		flags |= SDHCI_CMD_CRC;
	if (cmd->flags & MMC_RSP_OPCODE)
		flags |= SDHCI_CMD_INDEX;
	if (cmd->data)
		flags |= SDHCI_CMD_DATA;

	mode |= SDHCI_MAKE_CMD(cmd->opcode, flags);

	if ( (sdio_debug_level > 0) && (host->id == 1)) {
		printk("Complete sending cmd, transfer mode would be 0x%x\n", mode);
	}

	writel(mode, host->ioaddr + SDHCI_TRANSFER_MODE);

	mask = SDHCI_CMD_INHIBIT;
	timeout = (10*256) + 255;
	while (readl(host->ioaddr + SDHCI_PRESENT_STATE) & mask) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Controller never released(2) "
				"inhibit bit(s).\n", mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			cmd->error = -EIO;
			tasklet_schedule(&host->finish_tasklet);
		return;
		}
		timeout--;
        if (!(timeout & 0xFF)) 
		    mdelay(1);
	}
}

static void sdhci_finish_command(struct sdhci_host *host)
{
	int i;

	BUG_ON(host->cmd == NULL);

	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0; i < 4; i++) {
				host->cmd->resp[i] = readl(host->ioaddr +
							   SDHCI_RESPONSE + (3 -
									     i)
							   * 4) << 8;
				if (i != 3)
					host->cmd->resp[i] |=
					    readb(host->ioaddr +
						  SDHCI_RESPONSE + (3 - i) * 4 -
						  1);
			}
		} else {
			host->cmd->resp[0] =
			    readl(host->ioaddr + SDHCI_RESPONSE);
		}
	}

	host->cmd->error = 0;

	if (host->data && host->data_early)
		sdhci_finish_data(host);

	if (!host->cmd->data)
		tasklet_schedule(&host->finish_tasklet);

	host->cmd = NULL;
}

static void sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	/*This variable holds the value of clock divider, prescaler */
	int div = 0, prescaler = 0;
	int clk_rate = clk_get_rate(host->clk);
	u32 clk;
	unsigned long timeout;

	if (clock == 0) {
		goto out;
	} else {
		if (host->id != 1) {
			/* Non-SDIO slot */
			if (!host->plat_data->clk_flg) {
				clk_enable(host->clk);
				host->plat_data->clk_flg = 1;
			}
		}
		else {
			/* MMC1 */
			if (sdio_mmc1_clock) {
				if (!host->plat_data->clk_flg) {
					clk_enable(host->clk);
					host->plat_data->clk_flg = 1;
				}
			}
		}	
	}

	if (clock == host->clock)
		return;

	clk = readl(host->ioaddr + SDHCI_CLOCK_CONTROL) & ~SDHCI_CLOCK_MASK;
	writel(clk, host->ioaddr + SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		goto out;
	if (clock == host->min_clk)
		prescaler = 16;
	else
		prescaler = 0;
	while (prescaler <= 0x80) {
		for (div = 0; div <= 0xF; div++) {
			int x;
			if (prescaler != 0)
				x = (clk_rate / (div + 1)) / (prescaler * 2);
			else
				x = clk_rate / (div + 1);

			DBG("x=%d, clock=%d %d\n", x, clock, div);
			if (x <= clock)
				break;
		}
		if (div < 0x10)
			break;
		if (prescaler == 0)
			prescaler = 1;
		else
			prescaler <<= 1;
	}
	DBG("prescaler = 0x%x, divider = 0x%x\n", prescaler, div);
	clk |= (prescaler << 8) | (div << 4);

	/* Configure the clock control register */
	clk |=
	    (readl(host->ioaddr + SDHCI_CLOCK_CONTROL) & (~SDHCI_CLOCK_MASK));

	if (host->plat_data->vendor_ver < ESDHC_VENDOR_V22)
		writel(clk, host->ioaddr + SDHCI_CLOCK_CONTROL);
	else
		writel(clk | SDHCI_CLOCK_SD_EN,
		       host->ioaddr + SDHCI_CLOCK_CONTROL);

	/* Wait max 10 ms */
	timeout = 10;
	while (timeout > 0) {
		timeout--;
		mdelay(1);
	}

out:
	if (prescaler != 0)
		host->clock = (clk_rate / (div + 1)) / (prescaler * 2);
	else
		host->clock = clk_rate / (div + 1);
}

static void sdhci_set_power(struct sdhci_host *host, unsigned short power)
{
	int voltage = 0;

	/* There is no PWR CTL REG */
	if (host->power == power)
		return;

	if (host->regulator_mmc) {
		if (power == (unsigned short)-1) {
			regulator_disable(host->regulator_mmc);
			pr_debug("mmc power off\n");
		} else {
			if (power == 7)
				voltage = 1800000;
			else if (power >= 8)
				voltage = 2000000 + (power - 8) * 100000;
			regulator_set_voltage(host->regulator_mmc, voltage);

			if (regulator_enable(host->regulator_mmc) == 0) {
				DBG("mmc power on : %d\n",voltage);
				msleep(300);
			}
		}
	}

	host->power = power;
}

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

static void sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = mmc_priv(mmc);

	/* Enable the clock */
	if (host->id != 1) {
		if (!host->plat_data->clk_flg) {
			clk_enable(host->clk);
			host->plat_data->clk_flg = 1;
		}
	}
	else {
		if (sdio_mmc1_clock) {
			if (!host->plat_data->clk_flg) {
				clk_enable(host->clk);
				host->plat_data->clk_flg = 1;
			}
		}
	}

	if ((host->id == 1) && !sdio_mmc1_clock)
		sdhci_sdio_clk(1);

	spin_lock_irqsave(&host->lock, flags);

	WARN_ON(host->mrq != NULL);

	sdhci_activate_led(host);
	if (cpu_is_mx35_rev(CHIP_REV_2_0) < 0) {
		if (mrq->cmd && mrq->data) {
			if (mrq->data->flags & MMC_DATA_READ)
				last_op_dir = 1;
			else {
				if (last_op_dir)
					sdhci_reset(host,
						SDHCI_RESET_CMD |
						SDHCI_RESET_DATA);
			}
		}
	}
	spin_unlock_irqrestore(&host->lock, flags);

	host->mrq = mrq;
	if (!(host->flags & SDHCI_CD_PRESENT)) {
		host->mrq->cmd->error = -ENOMEDIUM;
		tasklet_schedule(&host->finish_tasklet);
	} else
		sdhci_send_command(host, mrq->cmd);

	mmiowb();
}

static void sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host;
	unsigned long flags;
	u32 tmp = 0;
	mxc_dma_device_t dev_id = 0;

	DBG("%s: clock %u, bus %lu, power %u, vdd %u\n", DRIVER_NAME,
	    ios->clock, 1UL << ios->bus_width, ios->power_mode, ios->vdd);

	host = mmc_priv(mmc);

	/* Configure the External DMA mode */
	if (host->flags & SDHCI_USE_EXTERNAL_DMA) {
		host->dma_dir = DMA_NONE;
		if (mmc->ios.bus_width != host->mode) {
			mxc_dma_free(host->dma);
			if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
				if (host->id == 0)
					dev_id = MXC_DMA_MMC1_WIDTH_4;
				else
					dev_id = MXC_DMA_MMC2_WIDTH_4;
			} else {
				if (host->id == 0)
					dev_id = MXC_DMA_MMC1_WIDTH_1;
				else
					dev_id = MXC_DMA_MMC2_WIDTH_1;
			}
			host->dma = mxc_dma_request(dev_id, "MXC MMC");
			if (host->dma < 0)
				printk("Cannot allocate MMC DMA channel\n");
			mxc_dma_callback_set(host->dma, sdhci_dma_irq,
					     (void *)host);
			/* Configure the WML rege */
			if (mxc_wml_value == 512)
				writel(SDHCI_WML_128_WORDS,
				       host->ioaddr + SDHCI_WML);
			else
				writel(SDHCI_WML_16_WORDS,
				       host->ioaddr + SDHCI_WML);
		}
	}

	host->mode = mmc->ios.bus_width;

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * Reset the chip on each power off.
	 * Should clear out any weird states.
	 */
	if (ios->power_mode == MMC_POWER_OFF) {
		writel(0, host->ioaddr + SDHCI_SIGNAL_ENABLE);
		sdhci_init(host);
	}

	sdhci_set_clock(host, ios->clock);

	if (ios->power_mode == MMC_POWER_OFF)
		sdhci_set_power(host, -1);
	else
		sdhci_set_power(host, ios->vdd);

	tmp = readl(host->ioaddr + SDHCI_HOST_CONTROL);

	if (ios->bus_width == MMC_BUS_WIDTH_8) {
                tmp &= ~SDHCI_CTRL_4BITBUS;
                tmp |= SDHCI_CTRL_8BITBUS;
		host->flags &= ~SDHCI_IN_4BIT_MODE;
        } else if (ios->bus_width == MMC_BUS_WIDTH_4) {
                tmp &= ~SDHCI_CTRL_8BITBUS;
                tmp |= SDHCI_CTRL_4BITBUS;
		host->flags |= SDHCI_IN_4BIT_MODE;
        } else if (ios->bus_width == MMC_BUS_WIDTH_1) {
                tmp &= ~SDHCI_CTRL_4BITBUS;
		tmp &= ~SDHCI_CTRL_8BITBUS;
		host->flags &= ~SDHCI_IN_4BIT_MODE;
        }
	if (host->flags & SDHCI_USE_DMA) 
		tmp |= SDHCI_CTRL_ADMA;

	writel(tmp, host->ioaddr + SDHCI_HOST_CONTROL);

	/*
	 * Some (ENE) controllers go apeshit on some ios operation,
	 * signalling timeout and CRC errors even on CMD0. Resetting
	 * it on each ios seems to solve the problem.
	 */
	if (host->chip->quirks & SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS)
		sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static int sdhci_get_ro(struct mmc_host *mmc)
{
	struct sdhci_host *host;

	host = mmc_priv(mmc);

	if (host->plat_data->wp_status)
		return host->plat_data->wp_status(mmc->parent);
	else
		return 0;
}

static void sdhci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sdhci_host *host;
	unsigned long flags;
	u32 ier, prot, clk, present;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	/* Enable the clock */
	if (host->id != 1) {
		if (!host->plat_data->clk_flg) {
			clk_enable(host->clk);
			host->plat_data->clk_flg = 1;
		}
	}
	else {
		if (sdio_mmc1_clock) {
			if (!host->plat_data->clk_flg) {
				clk_enable(host->clk);
				host->plat_data->clk_flg = 1;
			}
		}
	}

	sdhci_sdio_clk(1);

	ier = readl(host->ioaddr + SDHCI_INT_ENABLE);
	ier &= ~SDHCI_INT_CARD_INT;

	prot = readl(host->ioaddr + SDHCI_HOST_CONTROL);
	clk = readl(host->ioaddr + SDHCI_CLOCK_CONTROL);

	if (enable) {
		ier |= SDHCI_INT_CARD_INT;
		prot |= SDHCI_CTRL_D3CD;
		clk |= SDHCI_CLOCK_PER_EN | SDHCI_CLOCK_IPG_EN;
		present = readl(host->ioaddr + SDHCI_PRESENT_STATE);
		if ((present & SDHCI_CARD_INT_MASK) != SDHCI_CARD_INT_ID)
			writel(SDHCI_INT_CARD_INT,
				host->ioaddr + SDHCI_INT_STATUS);
	}
	else {
		ier &= ~SDHCI_INT_CARD_INT;
		prot &= ~SDHCI_CTRL_D3CD;
		clk &= ~(SDHCI_CLOCK_PER_EN | SDHCI_CLOCK_IPG_EN);
	}

	writel(ier, host->ioaddr + SDHCI_INT_ENABLE);
	writel(ier, host->ioaddr + SDHCI_SIGNAL_ENABLE);
	writel(prot, host->ioaddr + SDHCI_HOST_CONTROL);
	writel(clk, host->ioaddr + SDHCI_CLOCK_CONTROL);

	mmiowb();

	if (sdio_lpm_enabled)
		sdhci_sdio_clk(0);
	spin_unlock_irqrestore(&host->lock, flags);
}

static const struct mmc_host_ops sdhci_ops = {
	.request = sdhci_request,
	.set_ios = sdhci_set_ios,
	.get_ro = sdhci_get_ro,
	.enable_sdio_irq = sdhci_enable_sdio_irq,
};

/*****************************************************************************\
 *                                                                           *
 * Tasklets                                                                  *
 *                                                                           *
\*****************************************************************************/

static void sdhci_tasklet_card(unsigned long param)
{
	struct sdhci_host *host;
	unsigned long flags;
	unsigned int cd_status = 0;

	host = (struct sdhci_host *)param;

	if (host->flags & SDHCI_CD_PRESENT)
		host->flags &= ~SDHCI_CD_PRESENT;
	else
		host->flags |= SDHCI_CD_PRESENT;
	/* Detect there is a card in slot or not */
	DBG("cd_status=%d %s\n", cd_status,
	    (host->flags & SDHCI_CD_PRESENT) ? "inserted" : "removed");

	if (cd_status) {
		regulator_disable(vsd_reg);
	}
	else {
		regulator_enable(vsd_reg);
		regulator_set_voltage(vsd_reg, 3150000);
	}

	spin_lock_irqsave(&host->lock, flags);

	if (!(host->flags & SDHCI_CD_PRESENT)) {
		if (host->mrq) {
			printk(KERN_ERR "%s: Card removed during transfer!\n",
			       mmc_hostname(host->mmc));
			printk(KERN_ERR "%s: Resetting controller.\n",
			       mmc_hostname(host->mmc));

			sdhci_reset(host, SDHCI_RESET_CMD);
			sdhci_reset(host, SDHCI_RESET_DATA);

			host->mrq->cmd->error = -ENOMEDIUM;
			tasklet_schedule(&host->finish_tasklet);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));
}

static void sdhci_tasklet_finish(unsigned long param)
{
	struct sdhci_host *host;
	unsigned long flags;
	struct mmc_request *mrq;

	host = (struct sdhci_host *)param;

	spin_lock_irqsave(&host->lock, flags);

	del_timer(&host->timer);

	mrq = host->mrq;

	if (mrq->data && (mrq->data->error ||
		(mrq->data->stop && mrq->data->stop->error))) {
		printk(KERN_ERR "Data error on %d: %d\n", host->id, mrq->data->error);
	}			

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if (mrq->cmd->error ||
	    (mrq->data && (mrq->data->error ||
			   (mrq->data->stop && mrq->data->stop->error))) ||
	    (host->chip->quirks & SDHCI_QUIRK_RESET_AFTER_REQUEST)) {

		/* Some controllers need this kick or reset won't work here */
		if (host->chip->quirks & SDHCI_QUIRK_CLOCK_BEFORE_RESET) {
			unsigned int clock;

			/* This is to force an update */
			clock = host->clock;
			host->clock = 0;
			sdhci_set_clock(host, clock);
		}

		/* Spec says we should do both at the same time, but Ricoh
		   controllers do not like that. */
		sdhci_reset(host, SDHCI_RESET_CMD);
		sdhci_reset(host, SDHCI_RESET_DATA);
	}

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	if (host->id == 1)
		sdhci_idle_bus_adjust(host, 1);
	sdhci_deactivate_led(host);

	mmiowb();

	spin_unlock_irqrestore(&host->lock, flags);
	
	/* Stop the clock when the req is done */
	flags = SDHCI_DATA_ACTIVE | SDHCI_DOING_WRITE | SDHCI_DOING_READ;
	if (!(readl(host->ioaddr + SDHCI_PRESENT_STATE) & flags)) {
		if (host->id != 1) {
			if (host->plat_data->clk_flg) {
				clk_disable(host->clk);
				host->plat_data->clk_flg = 0;
			}
		}
		else {
			if (sdio_mmc1_clock) {
				if (host->plat_data->clk_flg) {
					clk_disable(host->clk);
					host->plat_data->clk_flg = 0;
				}
			}
		}
	}

	mmc_request_done(host->mmc, mrq);
}

static void sdhci_timeout_timer(unsigned long data)
{
	struct sdhci_host *host;
	unsigned long flags;

	host = (struct sdhci_host *)data;

	spin_lock_irqsave(&host->lock, flags);

	if (host->mrq) {
		printk(KERN_ERR "%s: Timeout waiting for hardware "
		       "interrupt.\n", mmc_hostname(host->mmc));
		sdhci_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			sdhci_finish_data(host);
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->mrq->cmd->error = -ETIMEDOUT;

			tasklet_schedule(&host->finish_tasklet);
		}
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void sdhci_cmd_irq(struct sdhci_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->cmd) {
		printk(KERN_ERR "%s: Got command interrupt 0x%08x even "
		       "though no command operation was in progress.\n",
		       mmc_hostname(host->mmc), (unsigned)intmask);
		sdhci_dumpregs(host);
		return;
	}

	if (intmask & SDHCI_INT_ACMD12ERR) {
		int tmp = 0;
		tmp = readl(host->ioaddr + SDHCI_ACMD12_ERR);
		if (tmp & (SDHCI_ACMD12_ERR_CE | SDHCI_ACMD12_ERR_IE |
			SDHCI_ACMD12_ERR_EBE))
				host->cmd->error = -EILSEQ;
		else if (tmp & SDHCI_ACMD12_ERR_TOE)
			host->cmd->error = -ETIMEDOUT;
	}

	if (intmask & SDHCI_INT_TIMEOUT)
		host->cmd->error = -ETIMEDOUT;
	else if (intmask & SDHCI_INT_CRC)
		host->cmd->error = -EILSEQ;
	else if (intmask & (SDHCI_INT_END_BIT | SDHCI_INT_INDEX))
		host->cmd->error = -EIO;

	if (host->cmd->error)
		tasklet_schedule(&host->finish_tasklet);
	else if (intmask & SDHCI_INT_RESPONSE)
		sdhci_finish_command(host);
}

static void sdhci_data_irq(struct sdhci_host *host, u32 intmask)
{
	u32 intsave = 0;

	BUG_ON(intmask == 0);

	if (!host->data) {
		/*
		 * A data end interrupt is sent together with the response
		 * for the stop command.
		 */
		if (intmask & SDHCI_INT_DATA_END)
			return;

		printk(KERN_ERR "%s: Got data interrupt 0x%08x even "
		       "though no data operation was in progress.\n",
		       mmc_hostname(host->mmc), (unsigned)intmask);
		sdhci_dumpregs(host);
		return;
	}

	/* Mask the INT */
	intsave = readl(host->ioaddr + SDHCI_INT_ENABLE);
	writel(intsave & (~(intmask & SDHCI_INT_DATA_RE_MASK)),
	       host->ioaddr + SDHCI_INT_ENABLE);

	if (intmask & SDHCI_INT_DATA_TIMEOUT)
		host->data->error = -ETIMEDOUT;
	else if (intmask & SDHCI_INT_DATA_CRC)
		host->data->error = -EILSEQ;
	else if (intmask & SDHCI_INT_DATA_END_BIT)
		host->data->error = -EIO;

	if (host->data->error)
		sdhci_finish_data(host);
	else {
		if ((host->flags & SDHCI_USE_EXTERNAL_DMA) &&
		    (host->dma_size >= mxc_wml_value)) {
			/* Use DMA if transfer size is greater than fifo size */
			if (intmask & (SDHCI_INT_DATA_AVAIL |
				       SDHCI_INT_SPACE_AVAIL)) {
				intsave &= ~SDHCI_INT_DATA_RE_MASK;
				if (mxc_dma_enable(host->dma) < 0) {
					printk(KERN_ERR "ENABLE SDMA ERR.\n");
					intsave |= SDHCI_INT_DATA_RE_MASK;
				}
			}
		} else {
			if (intmask & (SDHCI_INT_DATA_AVAIL |
				       SDHCI_INT_SPACE_AVAIL))
				sdhci_transfer_pio(host);
		}

		/*
		 * We currently don't do anything fancy with DMA
		 * boundaries, but as we can't disable the feature
		 * we need to at least restart the transfer.
		 */
		if ((intmask & SDHCI_INT_DMA_END) &&
		    (!(intmask & SDHCI_INT_DATA_END)))
			writel(readl(host->ioaddr + SDHCI_DMA_ADDRESS),
			       host->ioaddr + SDHCI_DMA_ADDRESS);

		if (intmask & SDHCI_INT_DATA_END) {
			if ((host->id == 0) && (host->data->flags & MMC_DATA_READ))
				writel(readl(host->ioaddr + SDHCI_CLOCK_CONTROL)
					& ~SDHCI_CLOCK_HLK_EN,
					host->ioaddr + SDHCI_CLOCK_CONTROL);

			if (host->cmd) {
				/*
				 * Data managed to finish before the
				 * command completed. Make sure we do
				 * things in the proper order.
				 */
				host->data_early = 1;
			} else {

				if (host->plat_data->vendor_ver
				    < ESDHC_VENDOR_V22) {
					/*
					 * There are the DATA END INT when
					 * writing is not complete. Double
					 * check on it. TO2 has been fixed it.
					 */
					intmask = readl(host->ioaddr +
							SDHCI_PRESENT_STATE);
					if (intmask & SDHCI_DATA_ACTIVE)
						goto data_irq_out;
				}
				sdhci_finish_data(host);
			}
		}
	}
      data_irq_out:
	/* Enable the INT */
	writel(intsave, host->ioaddr + SDHCI_INT_ENABLE);
}

/*!
* This function is called by DMA Interrupt Service Routine to indicate
* requested DMA transfer is completed.
*
* @param   devid  pointer to device specific structure
* @param   error any DMA error
* @param   cnt   amount of data that was transferred
*/
static void sdhci_dma_irq(void *devid, int error, unsigned int cnt)
{
	u32 intsave = 0;
	int ret;
	struct sdhci_host *host = devid;

	printk("%s: error: %d Transferred bytes:%d\n", DRIVER_NAME, error, cnt);
	if (host->flags & SDHCI_USE_EXTERNAL_DMA) {
		/*
		 * Stop the DMA transfer here, the data_irq would be called
		 * to process the others
		 */
		ret = mxc_dma_disable(host->dma);
		if (ret < 0)
			printk(KERN_ERR "Disable dma channel err %d\n", ret);

		if (error) {
			printk("Error in DMA transfer\n");
			return;
		}
		intsave = readl(host->ioaddr + SDHCI_INT_ENABLE);
		intsave |= SDHCI_INT_DATA_RE_MASK;
		writel(intsave, host->ioaddr + SDHCI_INT_ENABLE);
	}
}

/* woke queue handler func */
static void esdhc_cd_callback(struct work_struct *work)
{
	unsigned long flags;
	unsigned int cd_status = 0;
	struct sdhci_host *host = container_of(work, struct sdhci_host, cd_wq);

	cd_status = host->plat_data->status(host->mmc->parent);
	if (cd_status)
		host->flags &= ~SDHCI_CD_PRESENT;
	else
		host->flags |= SDHCI_CD_PRESENT;
	/* Detect there is a card in slot or not */
	printk("cd_status=%d %s\n", cd_status,
	    (host->flags & SDHCI_CD_PRESENT) ? "inserted" : "removed");

	if (cd_status) {
		regulator_disable(vsd_reg);
	}
	else {
		regulator_enable(vsd_reg);
		regulator_set_voltage(vsd_reg, 3150000);
		mdelay(100);
	}

	spin_lock_irqsave(&host->lock, flags);

	if (!(host->flags & SDHCI_CD_PRESENT)) {
		printk(KERN_ERR
			"%s: Card removed and resetting controller.\n",
			mmc_hostname(host->mmc));
		sdhci_init(host);
		if (host->mrq) {
			printk(KERN_ERR
			       "%s: Card removed during transfer!\n",
			       mmc_hostname(host->mmc));
			printk(KERN_ERR
			       "%s: Resetting controller.\n",
			       mmc_hostname(host->mmc));

			sdhci_reset(host, SDHCI_RESET_CMD);
			sdhci_reset(host, SDHCI_RESET_DATA);

			host->mrq->cmd->error = -ENOMEDIUM;
			tasklet_schedule(&host->finish_tasklet);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));

	do {
		cd_status = host->plat_data->status(host->mmc->parent);
		if (cd_status)
			set_irq_type(host->detect_irq, IRQT_FALLING);
		else
			set_irq_type(host->detect_irq, IRQT_RISING);
	} while (cd_status != host->plat_data->status(host->mmc->parent));
}

/*!
* Card detection interrupt service routine registered to handle
* the SDHC interrupts. This interrupt routine handles card
* insertion and card removal interrupts.
*
* @param   irq    the interrupt number
* @param   devid  driver private data
*
* @return  The function returns \b IRQ_RETVAL(1)
*/
static irqreturn_t sdhci_cd_irq(int irq, void *dev_id)
{
	struct sdhci_host *host = dev_id;

	schedule_work(&host->cd_wq);
	return IRQ_HANDLED;
}

static irqreturn_t sdhci_irq(int irq, void *dev_id)
{
	irqreturn_t result;
	struct sdhci_host *host = dev_id;
	u32 intmask;
	int cardint = 0;

	spin_lock(&host->lock);

	if (host->id == 1)
		sdhci_sdio_clk(1);

	intmask = readl(host->ioaddr + SDHCI_INT_STATUS);

	if (!intmask || intmask == 0xffffffff) {
		result = IRQ_NONE;
		goto out;
	}

	DBG("*** %s got interrupt: 0x%08x\n", mmc_hostname(host->mmc), intmask);

	if (intmask & (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE)) {
		writel(intmask &
		       (SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE),
		       host->ioaddr + SDHCI_INT_STATUS);
		tasklet_schedule(&host->card_tasklet);
	}

	intmask &= ~(SDHCI_INT_CARD_INSERT | SDHCI_INT_CARD_REMOVE);

	if (intmask & SDHCI_INT_CMD_MASK) {
		writel(intmask & SDHCI_INT_CMD_MASK,
		       host->ioaddr + SDHCI_INT_STATUS);
		sdhci_cmd_irq(host, intmask & SDHCI_INT_CMD_MASK);
	}

	if (intmask & SDHCI_INT_DATA_MASK) {
		writel(intmask & SDHCI_INT_DATA_MASK,
		       host->ioaddr + SDHCI_INT_STATUS);
		if (cpu_is_mx35_rev(CHIP_REV_2_0) < 0) {
			if (!
			    (readl(host->ioaddr + SDHCI_TRANSFER_MODE) &
			     SDHCI_TRNS_READ))
				intmask &= ~SDHCI_INT_DATA_END_BIT;
		}
		sdhci_data_irq(host, intmask & SDHCI_INT_DATA_MASK);
	}

	intmask &= ~(SDHCI_INT_CMD_MASK | SDHCI_INT_DATA_MASK);

	intmask &= ~SDHCI_INT_ERROR;

	if (intmask & SDHCI_INT_BUS_POWER) {
		printk(KERN_ERR "%s: Card is consuming too much power!\n",
		       mmc_hostname(host->mmc));
		writel(SDHCI_INT_BUS_POWER, host->ioaddr + SDHCI_INT_STATUS);
	}

	intmask &= ~SDHCI_INT_BUS_POWER;

	if (intmask & SDHCI_INT_CARD_INT) {
		if (host->id == 1)
			cardint_stat++;
		if (readl(host->ioaddr + SDHCI_INT_ENABLE) & SDHCI_INT_CARD_INT) {
			cardint = 1;
		}
	}

	intmask &= ~SDHCI_INT_CARD_INT;

	if (intmask) {
		printk(KERN_ERR "%s: Unexpected interrupt 0x%08x.\n",
		       mmc_hostname(host->mmc), intmask);
		sdhci_dumpregs(host);

		writel(intmask, host->ioaddr + SDHCI_INT_STATUS);
	}

	result = IRQ_HANDLED;

	mmiowb();
out:
	spin_unlock(&host->lock);

	/*
	 * We have to delay this as it calls back into the driver.
	 */
	if (cardint) {
		cardint_sdio_irq++;
		sdhci_sdio_clk(1);
		mmc_signal_sdio_irq(host->mmc);
	}

	return result;
}

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static int sdhci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sdhci_chip *chip;
	int i, ret;

	chip = dev_get_drvdata(&pdev->dev);
	if (!chip)
		return 0;

	/*
	 * Bus #1 is the SDIO bus for the Atheros WiFi chip. Simply gate the clock
	 * and deactivate the GPIOs. We dont want to call mmc_suspend_host() as 
	 * SDIO does not have a suspend/resume function. SDIO stack will simply
	 * remove the MMC1 card and try to re-attach. This is not acceptable to
	 * all cards.
	 */
	if (pdev->id == 1) {
		clk_disable(chip->hosts[0]->clk);
		goto out_suspend;
	}

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;
		ret = mmc_suspend_host(chip->hosts[i]->mmc, state);
		if (ret) {
			for (i--; i >= 0; i--)
				mmc_resume_host(chip->hosts[i]->mmc);
			return ret;
		}
	}

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;

		if (i == 2)
			regulator_disable(vsd_reg);

		free_irq(chip->hosts[i]->detect_irq, chip->hosts[i]);
		free_irq(chip->hosts[i]->irq, chip->hosts[i]);
		clk_disable(chip->hosts[i]->clk);
	}

out_suspend:
	gpio_sdhc_inactive(pdev->id);

	return 0;
}

static int sdhci_resume(struct platform_device *pdev)
{
	struct sdhci_chip *chip;
	int i, ret;
	unsigned int cd_status = 0;

	chip = dev_get_drvdata(&pdev->dev);
	if (!chip)
		return 0;

	gpio_sdhc_active(pdev->id);

	/*
	 * MMC1 is the SDIO stack for Atheros chip
	 */
	if (pdev->id == 1) {
		clk_enable(chip->hosts[0]->clk);
		chip->hosts[0]->plat_data->clk_flg = 1;
		schedule_delayed_work(&sdio_mmc1_resume_wq, msecs_to_jiffies(SDIO_CLK_RESUME_THRESHOLD));
		return 0;
	}

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;

		if (i == 2) {
			regulator_enable(vsd_reg);
			regulator_set_voltage(vsd_reg, 3150000);
			mdelay(10);
		}

		ret = request_irq(chip->hosts[i]->detect_irq, sdhci_cd_irq,
				IRQF_DISABLED,
				mmc_hostname(chip->hosts[i]->mmc),
				chip->hosts[i]);

		if (ret)
			return ret;

		ret = request_irq(chip->hosts[i]->irq, sdhci_irq,
				  IRQF_SHARED,
				  mmc_hostname(chip->hosts[i]->mmc),
				  chip->hosts[i]);
		if (ret)
			return ret;
		sdhci_init(chip->hosts[i]);
		mmiowb();
		
		cd_status = chip->hosts[i]->plat_data->status(chip->hosts[i]->mmc->parent);
		if (cd_status)
			chip->hosts[i]->flags &= ~SDHCI_CD_PRESENT;
		else
			chip->hosts[i]->flags |= SDHCI_CD_PRESENT;
		
		ret = mmc_resume_host(chip->hosts[i]->mmc);
		if (ret)
			return ret;

		if ((i == 2) && cd_status) {
			ret = regulator_disable(vsd_reg);
			if (ret < 0)
				printk(KERN_ERR "Failed to turn off VSD regulator: %d\n", ret);
		}
		clk_disable(chip->hosts[i]->clk);
		chip->hosts[i]->plat_data->clk_flg = 0;
	}

	return 0;
}

#else				/* CONFIG_PM */

#define sdhci_suspend NULL
#define sdhci_resume NULL

#endif				/* CONFIG_PM */

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_FEC
#define HAS_8BIT_MMC()	0
#else
#define HAS_8BIT_MMC()	((IS_LUIGI() && (GET_BOARD_HW_VERSION() >= 4)) || \
						 (IS_SHASTA() && !(IS_EVT() && (GET_BOARD_HW_VERSION() == 1))))
#endif

static int __devinit sdhci_probe_slot(struct platform_device
				      *pdev, int slot)
{
	struct mxc_mmc_platform_data *mmc_plat = pdev->dev.platform_data;
	int ret = 0;
	unsigned int version, caps;
	struct sdhci_chip *chip;
	struct mmc_host *mmc;
	struct sdhci_host *host;
	mxc_dma_device_t dev_id = 0;

	if (!mmc_plat)
		return -EINVAL;

	chip = dev_get_drvdata(&pdev->dev);
	BUG_ON(!chip);

	mmc = mmc_alloc_host(sizeof(struct sdhci_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dma = -1;
	host->plat_data = mmc_plat;
	if (!host->plat_data) {
		ret = -EINVAL;
		goto out0;
	}

	if (pdev->id == 1)
		sdio_host = host;

	host->chip = chip;
	chip->hosts[slot] = host;

	/* Active the eSDHC bus */
	gpio_sdhc_active(pdev->id);

	vsd_reg = regulator_get(NULL, "VSD");
	regulator_disable(vsd_reg);
	if (IS_SHASTA())
		mdelay(5);
	else
		mdelay(500);
	regulator_enable(vsd_reg);
	regulator_set_voltage(vsd_reg, 3150000);

	host->id = pdev->id;

	if ((pdev->id == 2) && host->plat_data->status(host->mmc->parent)) {
		regulator_disable(vsd_reg);
		mdelay(10);
	}
	
	/* Get the SDHC clock from clock system APIs */
	host->clk = clk_get(&pdev->dev, mmc_plat->clock_mmc);
	if (NULL == host->clk)
		printk(KERN_ERR "MXC MMC can't get clock.\n");
	DBG("SDHC:%d clock:%lu\n", pdev->id, clk_get_rate(host->clk));

	host->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!host->res) {
		ret = -ENOMEM;
		goto out1;
	}
	host->irq = platform_get_irq(pdev, 0);

	if (!host->irq) {
		ret = -ENOMEM;
		goto out1;
	}

	host->detect_irq = platform_get_irq(pdev, 1);
	if (!host->detect_irq) {
		ret = -ENOMEM;
		goto out1;
	}

	do {
		ret = host->plat_data->status(host->mmc->parent);
		if (ret)
			set_irq_type(host->detect_irq, IRQT_FALLING);
		else
			set_irq_type(host->detect_irq, IRQT_RISING);
	} while (ret != host->plat_data->status(host->mmc->parent));

	ret = host->plat_data->status(host->mmc->parent);
	if (ret)
		host->flags &= ~SDHCI_CD_PRESENT;
	else
		host->flags |= SDHCI_CD_PRESENT;

	DBG("slot %d at 0x%x, irq %d\n", slot, host->res->start, host->irq);

	if (!request_mem_region(host->res->start,
				resource_size(host->res), pdev->name)) {
		printk(KERN_ERR "request_mem_region failed\n");
		ret = -ENOMEM;
		goto out1;
	}
	host->ioaddr = (void *)ioremap(host->res->start,
					resource_size(host->res));
	if (!host->ioaddr) {
		ret = -ENOMEM;
		goto out3;
	}

	sdhci_reset(host, SDHCI_RESET_ALL);

	version = readl(host->ioaddr + SDHCI_HOST_VERSION);
	host->plat_data->vendor_ver = (version & SDHCI_VENDOR_VER_MASK) >>
	    SDHCI_VENDOR_VER_SHIFT;
	version = (version & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT;
	if (version != 1) {
		printk(KERN_ERR "%s: Unknown controller version (%d). "
		       "You may experience problems.\n", mmc_hostname(mmc),
		       version);
	}

	caps = readl(host->ioaddr + SDHCI_CAPABILITIES);

	if (chip->quirks & SDHCI_QUIRK_FORCE_DMA)
		host->flags |= SDHCI_USE_DMA;
	else if (!(caps & SDHCI_CAN_DO_DMA))
		printk("Controller doesn't have DMA capability\n");
	else if (chip->
		 quirks & (SDHCI_QUIRK_INTERNAL_ADVANCED_DMA |
			   SDHCI_QUIRK_INTERNAL_SIMPLE_DMA))
		host->flags |= SDHCI_USE_DMA;
	else if (chip->quirks & (SDHCI_QUIRK_EXTERNAL_DMA_MODE))
		host->flags |= SDHCI_USE_EXTERNAL_DMA;
	else
		host->flags &= ~SDHCI_USE_DMA;

	/*
	 * These definitions of eSDHC are not compatible with the SD Host
	 * Controller Spec v2.0
	 */
	host->min_clk = mmc_plat->min_clk;
	host->max_clk = mmc_plat->max_clk;
	host->timeout_clk = 1024 * 1000;	/* Just set the value temply. */

	/*
	 * Set host parameters.
	 */
	mmc->ops = &sdhci_ops;
	mmc->f_min = host->min_clk;
	mmc->f_max = host->max_clk;

	/* Third slot is the SD card */
	if (host->id == 0) {
		mmc->caps = MMC_CAP_MMC_HIGHSPEED;

		if ( HAS_8BIT_MMC() ) {
				mmc->caps |= MMC_CAP_8_BIT_DATA;
				printk(KERN_INFO "%s: Using 8-bit bus width\n", mmc_hostname(mmc));
		}
		else {
			mmc->caps |= MMC_CAP_4_BIT_DATA;
			printk(KERN_INFO "%s: Using 4-bit bus width\n", mmc_hostname(mmc));
		}
	}
	else
		mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ;

	mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NON_REMOVABLE;

	mmc->ocr_avail = 0;

	if (caps & SDHCI_CAN_VDD_330)
		mmc->ocr_avail |= MMC_VDD_32_33 | MMC_VDD_33_34;
	if (caps & SDHCI_CAN_VDD_300)
		mmc->ocr_avail |= MMC_VDD_29_30 | MMC_VDD_30_31;
	if (caps & SDHCI_CAN_VDD_180)
		mmc->ocr_avail |= MMC_VDD_165_195;

	if (mmc->ocr_avail == 0) {
		printk(KERN_ERR "%s: Hardware doesn't report any "
		       "support voltages.\n", mmc_hostname(mmc));
		ret = -ENODEV;
		goto out3;
	}

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of segments. Hardware cannot do scatter lists.
	 */
	if (host->flags & SDHCI_USE_DMA)
		mmc->max_hw_segs = 1;
	else
		mmc->max_hw_segs = 16;
	mmc->max_phys_segs = 16;

	/*
	 * Maximum number of sectors in one transfer. Limited by DMA boundary
	 * size (512KiB).
	 */
	if (host->flags & SDHCI_USE_EXTERNAL_DMA)
		mmc->max_req_size = 32 * 1024;
	else
		mmc->max_req_size = 64 * 1024;

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes.
	 */
	mmc->max_seg_size = mmc->max_req_size;

	/*
	 * Maximum block size. This varies from controller to controller and
	 * is specified in the capabilities register.
	 */
	mmc->max_blk_size =
	    (caps & SDHCI_MAX_BLOCK_MASK) >> SDHCI_MAX_BLOCK_SHIFT;
	if (mmc->max_blk_size > 3) {
		printk(KERN_WARNING "%s: Invalid maximum block size, "
		       "assuming 512 bytes\n", mmc_hostname(mmc));
		mmc->max_blk_size = 512;
	} else
		mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count: SD cards can take more blocks
	 */
	mmc->max_blk_count = 128;

	/*
	 * Apply a continous physical memory used for storing the ADMA
	 * descriptor table.
	 */
	if (host->flags & SDHCI_USE_DMA) {
		adma_des_table = kcalloc((2 * (mmc->max_phys_segs) + 1),
					 sizeof(unsigned int), GFP_DMA);
		if (adma_des_table == NULL) {
			printk(KERN_ERR "Cannot allocate ADMA memory\n");
			ret = -ENOMEM;
			goto out3;
		}
	}

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->card_tasklet,
		     sdhci_tasklet_card, (unsigned long)host);
	tasklet_init(&host->finish_tasklet,
		     sdhci_tasklet_finish, (unsigned long)host);

	/* initialize the work queue */
	INIT_WORK(&host->cd_wq, esdhc_cd_callback);

	setup_timer(&host->timer, sdhci_timeout_timer, (unsigned long)host);

	ret = request_irq(host->detect_irq, sdhci_cd_irq, 0, pdev->name, host);
	if (ret)
		goto out4;

	ret = request_irq(host->irq, sdhci_irq, IRQF_SHARED, pdev->name, host);
	if (ret)
		goto out5;

	sdhci_init(host);

	if (host->flags & SDHCI_USE_EXTERNAL_DMA) {
		/* Apply the 1-bit SDMA channel. */
		if (host->id == 0)
			dev_id = MXC_DMA_MMC1_WIDTH_1;
		else
			dev_id = MXC_DMA_MMC2_WIDTH_1;
		host->dma = mxc_dma_request(dev_id, "MXC MMC");
		if (host->dma < 0) {
			printk("Cannot allocate MMC DMA channel\n");
			goto out6;
		}
		mxc_dma_callback_set(host->dma, sdhci_dma_irq, (void *)host);
	}
#ifdef CONFIG_MMC_DEBUG
	sdhci_dumpregs(host);
#endif
	mmiowb();

	if (mmc_add_host(mmc) < 0)
		goto out6;

	if (host->flags & SDHCI_USE_EXTERNAL_DMA)
		printk(KERN_INFO "%s: SDHCI detect irq %d irq %d %s\n",
		       mmc_hostname(mmc), host->detect_irq, host->irq,
		       "EXTERNAL DMA");
	else
		printk(KERN_INFO "%s: SDHCI detect irq %d irq %d %s\n",
		       mmc_hostname(mmc), host->detect_irq, host->irq,
		       (host->flags & SDHCI_USE_DMA) ? "INTERNAL DMA" : "PIO");

	if (device_create_file(&pdev->dev, &dev_attr_sdhci_registers) < 0) {
		printk (KERN_ERR "mx_sdhci: could not create the sdhci_registers /sys\n");
	}

	if (device_create_file(&pdev->dev, &dev_attr_sdio_debug) < 0) {
		printk (KERN_ERR "mx_sdhci: could not create the sdio_debug /sys\n");
	}

	if (device_create_file(&pdev->dev, &dev_attr_sdio_pio_transfers) < 0) {
		printk (KERN_ERR "mx_sdhci: could not create the sdio_pio_transfers /sys\n");
	}

	if (device_create_file(&pdev->dev, &dev_attr_sdio_lpm_idle) < 0) {
		printk (KERN_ERR "mx_sdhci: could not create the sdio_lpm_idle /sys\n");
	}

	if (device_create_file(&pdev->dev, &dev_attr_sdio_lpm_threshold) < 0) {
		printk (KERN_ERR "mx_sdhci: could not create the dev_attr_sdio_lpm_threshold /sys\n");
	}

	return 0;

out6:
	free_irq(host->irq, host);
out5:
	if (host->detect_irq)
		free_irq(host->detect_irq, host);
out4:
	del_timer_sync(&host->timer);
	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);
out3:
	if (host->flags & SDHCI_USE_DMA)
		kfree(adma_des_table);
	release_mem_region(host->res->start,
				resource_size(host->res));
out1:
	clk_disable(host->clk);
	host->plat_data->clk_flg = 0;
	gpio_sdhc_inactive(pdev->id);
out0:
	mmc_free_host(mmc);
	return ret;
}

static void sdhci_remove_slot(struct platform_device *pdev, int slot)
{
	struct sdhci_chip *chip;
	struct mmc_host *mmc;
	struct sdhci_host *host;

	chip = dev_get_drvdata(&pdev->dev);
	host = chip->hosts[slot];
	mmc = host->mmc;

	clk_enable(host->clk);

	mmc_remove_host(mmc);

	sdhci_reset(host, SDHCI_RESET_ALL);

	if (host->detect_irq)
		free_irq(host->detect_irq, host);
	free_irq(host->irq, host);
	if (chip->quirks & SDHCI_QUIRK_EXTERNAL_DMA_MODE) {
		host->flags &= ~SDHCI_USE_EXTERNAL_DMA;
		mxc_dma_free(host->dma);
	}

	del_timer_sync(&host->timer);

	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);

	if (host->flags & SDHCI_USE_DMA)
		kfree(adma_des_table);
	release_mem_region(host->res->start, resource_size(host->res));

	host->plat_data->clk_flg = 0;
	mmc_free_host(mmc);
	gpio_sdhc_inactive(pdev->id);

	clk_disable(host->clk);

	if (pdev->id == 2)
		regulator_disable(vsd_reg);

	device_remove_file(&pdev->dev, &dev_attr_sdhci_registers);
	device_remove_file(&pdev->dev, &dev_attr_sdio_debug);
	device_remove_file(&pdev->dev, &dev_attr_sdio_pio_transfers);
	device_remove_file(&pdev->dev, &dev_attr_sdio_lpm_idle);
	device_remove_file(&pdev->dev, &dev_attr_sdio_lpm_threshold);
}

static int sdhci_probe(struct platform_device *pdev)
{
	int ret = 0, i;
	u8 slots = 1;
	struct sdhci_chip *chip;

	printk(KERN_INFO DRIVER_NAME ": MXC SDHCI Controller Driver. \n");
	BUG_ON(pdev == NULL);

	chip = kzalloc(sizeof(struct sdhci_chip) +
		       sizeof(struct sdhci_host *) * slots, GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto err;
	}

	/* Distinguish different platform */
	if (machine_is_mx37_3ds()) {
		mxc_quirks = SDHCI_QUIRK_EXTERNAL_DMA_MODE;
	} else {
		mxc_quirks = SDHCI_QUIRK_INTERNAL_ADVANCED_DMA |
		    SDHCI_QUIRK_INTERNAL_SIMPLE_DMA;
	}
	chip->pdev = pdev;
	chip->quirks = mxc_quirks;

	if (debug_quirks)
		chip->quirks = debug_quirks;

	chip->num_slots = slots;
	dev_set_drvdata(&pdev->dev, chip);

	for (i = 0; i < slots; i++) {
		ret = sdhci_probe_slot(pdev, i);
		if (ret) {
			for (i--; i >= 0; i--)
				sdhci_remove_slot(pdev, i);
			goto free;
		}
	}

	return 0;

      free:
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(chip);

      err:
	return ret;
}

static int sdhci_remove(struct platform_device *pdev)
{
	int i;
	struct sdhci_chip *chip;

	chip = dev_get_drvdata(&pdev->dev);

	if (chip) {
		for (i = 0; i < chip->num_slots; i++)
			sdhci_remove_slot(pdev, i);

		dev_set_drvdata(&pdev->dev, NULL);

		kfree(chip);
	}

	return 0;
}

/* Invoked on Shutdown */
static void sdhci_shutdown(struct platform_device *pdev)
{
	sdhci_remove(pdev);
}

static struct platform_driver sdhci_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.probe = sdhci_probe,
	.remove = sdhci_remove,
	.suspend = sdhci_suspend,
	.resume = sdhci_resume,
	.shutdown = sdhci_shutdown,
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_drv_init(void)
{
	printk(KERN_INFO DRIVER_NAME
	       ": MXC Secure Digital Host Controller Interface driver\n");
	return platform_driver_register(&sdhci_driver);
}

static void __exit sdhci_drv_exit(void)
{
	DBG("Exiting\n");

	platform_driver_unregister(&sdhci_driver);
}

module_init(sdhci_drv_init);
module_exit(sdhci_drv_exit);

module_param(debug_quirks, uint, 0444);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug_quirks, "Force certain quirks.");
