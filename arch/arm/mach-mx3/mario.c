/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/delay.h>
#ifdef CONFIG_KGDB_8250
#include <linux/kgdb.h>
#endif
#include <linux/input.h>
#include <linux/nodemask.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#if defined(CONFIG_MTD) || defined(CONFIG_MTD_MODULE)
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/flash.h>
#endif

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/keypad.h>
#include <asm/arch/memory.h>
#include <asm/arch/gpio.h>
#include <asm/arch/pmic_power.h>
#include <asm/arch/boot_globals.h>
#include <linux/bootmem.h>

#include "crm_regs.h"
#include "iomux.h"
/*!
 * @file mach-mx3/mario.c
 *
 * @brief This file contains the board specific initialization routines.
 *
 * @ingroup MSL_MX31
 */

extern void mxc_map_io(void);
extern void mxc_init_irq(void);
extern void mxc_cpu_init(void) __init;
extern void mario_gpio_init(void) __init;
extern struct sys_timer mxc_timer;
extern void mxc_cpu_common_init(void);
extern int mxc_clocks_init(void);

static void mxc_nop_release(struct device *dev)
{
	/* Nothing */
}

unsigned long board_get_ckih_rate(void)
{
	return CKIH_CLK_FREQ;
}

#if defined(CONFIG_KEYBOARD_MXC) || defined(CONFIG_KEYBOARD_MXC_MODULE)

/* Keypad keycodes for the keypads.
 * NOTE: for each keypad supported, add the same number of keys (64);
 *       otherwise the mapping will NOT work!
 */

// *** IMPORTANT!  Change the following parameter when adding a new KB map
//                 to the array below.
#define NUMBER_OF_KB_MAPPINGS 6

static u16 keymapping[NUMBER_OF_KB_MAPPINGS][64] = { 

// keymapping[0] = Key mapping for the old Fiona keypad.
// NOTE: 64 keys; even those unused

     {
        // Row 0
	KEY_RESERVED, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_RESERVED,
        // Row 1
	KEY_RESERVED, KEY_7, KEY_8, KEY_9, KEY_0, KEY_Q, KEY_W, KEY_RESERVED,
        // Row 2
	KEY_RESERVED, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_BACKSPACE,
        // Row 3
	KEY_RESERVED, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D, KEY_F, KEY_ENTER,
        // Row 4
	KEY_RESERVED, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_DOT,
        // Row 5
	KEY_RESERVED, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_KATAKANA,
        // Row 6
	KEY_RESERVED, KEY_LEFTALT, KEY_SPACE, KEY_RESERVED,
	KEY_RESERVED, KEY_PAGEDOWN, KEY_PAGEUP, KEY_RESERVED,
        // Row 7
	KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
	KEY_RESERVED, KEY_RESERVED, KEY_RESERVED, KEY_RESERVED,
     },

// keymapping[1] = key mapping for Turing v0 (first build) with trackball.
// NOTE: 64 keys; even those unused

     {
        // Row 0
	KEY_RESERVED, KEY_6, KEY_R, KEY_S, KEY_BACKSPACE, KEY_DOT,
	KEY_KPSLASH /* Home */, KEY_RESERVED,
	// Row 1
	KEY_RESERVED, KEY_7, KEY_T, KEY_D, KEY_Z, KEY_MUHENKAN /* SYM */,
	KEY_VOLUMEUP, KEY_RESERVED,
	// Row 2
	KEY_RESERVED, KEY_8, KEY_Y, KEY_F, KEY_X, KEY_ENTER, KEY_VOLUMEDOWN,
	KEY_RESERVED,
	// Row 3
	KEY_1, KEY_9, KEY_U, KEY_G, KEY_C, KEY_LEFTSHIFT, KEY_MENU,
	KEY_RESERVED,
	// Row 4
	KEY_2, KEY_0, KEY_I, KEY_H, KEY_V, KEY_LEFTALT, KEY_HIRAGANA /* Back */,
	KEY_RESERVED,
	// Row 5
	KEY_3, KEY_Q, KEY_O, KEY_J, KEY_B, KEY_SPACE,
	KEY_HENKAN /* Trackball Select */, KEY_RESERVED,
        // Row 6
        KEY_4, KEY_W, KEY_P, KEY_K, KEY_N, KEY_KATAKANA /* Font Size */,
	KEY_RESERVED, KEY_RESERVED,
	// Row 7
	KEY_5, KEY_E, KEY_A, KEY_L, KEY_M, KEY_RIGHTSHIFT, KEY_RESERVED,
	KEY_RESERVED,
     },

// keymapping[2] = key mapping for Turing v0 (first build) with 5-Way
// NOTE: 64 keys; even those unused

     {
        // Row 0
	KEY_RESERVED, KEY_6, KEY_R, KEY_S, KEY_BACKSPACE, KEY_DOT,
	KEY_LEFT /* 5-Way Left */, KEY_RESERVED,
	// Row 1
	KEY_RESERVED, KEY_7, KEY_T, KEY_D, KEY_Z, KEY_MUHENKAN /* SYM */,
	KEY_VOLUMEUP, KEY_RESERVED,
	// Row 2
	KEY_RESERVED, KEY_8, KEY_Y, KEY_F, KEY_X, KEY_ENTER, KEY_VOLUMEDOWN,
	KEY_RESERVED,
	// Row 3
	KEY_1, KEY_9, KEY_U, KEY_G, KEY_C, KEY_LEFTSHIFT,
	KEY_HANJA /* 5-Way Down */, KEY_RESERVED,
	// Row 4
	KEY_2, KEY_0, KEY_I, KEY_H, KEY_V, KEY_LEFTALT,
	KEY_HENKAN /* 5-Way Select */, KEY_RESERVED,
	// Row 5
	KEY_3, KEY_Q, KEY_O, KEY_J, KEY_B, KEY_SPACE,
	KEY_RIGHT /* 5-Way Right */, KEY_RESERVED,
        // Row 6
        KEY_4, KEY_W, KEY_P, KEY_K, KEY_N, KEY_KATAKANA /* Font Size */,
	KEY_HANGEUL /* 5-Way Up */, KEY_RESERVED,
	// Row 7
	KEY_5, KEY_E, KEY_A, KEY_L, KEY_M, KEY_RIGHTSHIFT, KEY_RESERVED,
	KEY_RESERVED,
     },

// keymapping[3] = key mapping for Turing EVT1 (with 5-Way input device)
// NOTE: 64 keys; even those unused

     {
        // Row 0
	KEY_RESERVED, KEY_6, KEY_R, KEY_S, KEY_BACKSPACE, KEY_DOT,
	KEY_HIRAGANA /* Back */, KEY_RESERVED,
	// Row 1
	KEY_RESERVED, KEY_7, KEY_T, KEY_D, KEY_Z, KEY_SLASH,
	KEY_VOLUMEUP, KEY_RESERVED,
	// Row 2
	KEY_RESERVED, KEY_8, KEY_Y, KEY_F, KEY_X, KEY_ENTER, KEY_VOLUMEDOWN,
	KEY_RESERVED,
	// Row 3
	KEY_1, KEY_9, KEY_U, KEY_G, KEY_C, KEY_LEFTSHIFT,
	KEY_KPSLASH /* Home */, KEY_RESERVED,
	// Row 4
	KEY_2, KEY_0, KEY_I, KEY_H, KEY_V, KEY_LEFTALT,
	KEY_MENU, KEY_RESERVED,
	// Row 5
	KEY_3, KEY_Q, KEY_O, KEY_J, KEY_B, KEY_SPACE,
	KEY_YEN /* Next-Page right button */, KEY_RESERVED,
        // Row 6
        KEY_4, KEY_W, KEY_P, KEY_K, KEY_N, KEY_KATAKANA /* Font Size */,
	KEY_PAGEDOWN /* Prev-page */, KEY_RESERVED,
	// Row 7
	KEY_5, KEY_E, KEY_A, KEY_L, KEY_M, KEY_MUHENKAN /* SYM */,
	KEY_PAGEUP /* Next-Page left button */, KEY_RESERVED,
     },

// keymapping[4] = key mapping for Nell EVT1
// NOTE: 64 keys; even those unused
// NOTE: The number keys in Nell are combined with the first
//       row of letters (Q/1, W/2, etc).  We still need to define
//       the number key codes in the matrix so that they are enabled
//       for the input event driver.  Since we cannot define two key codes
//       in one row/col entry, these 10 keycodes are defined in unused
//       row/col entries.

     {
        // Row 0
	KEY_RESERVED, KEY_6, KEY_R, KEY_S, KEY_BACKSPACE, KEY_DOT,
	KEY_PAGEDOWN /* Prev-page */, KEY_RESERVED,
	// Row 1
	KEY_RESERVED, KEY_7, KEY_T, KEY_D, KEY_Z, KEY_SLASH,
	KEY_VOLUMEUP, KEY_RESERVED,
	// Row 2
	KEY_RESERVED, KEY_8, KEY_Y, KEY_F, KEY_X, KEY_ENTER,
	KEY_VOLUMEDOWN, KEY_RESERVED,
	// Row 3
	KEY_1, KEY_9, KEY_U, KEY_G, KEY_C, KEY_LEFTSHIFT,
	KEY_KPSLASH /* Home */, KEY_RESERVED,
	// Row 4
	KEY_2, KEY_0, KEY_I, KEY_H, KEY_V, KEY_LEFTALT,
	KEY_YEN /* Next-Page right button */, KEY_RESERVED,
	// Row 5
	KEY_3, KEY_Q, KEY_O, KEY_J, KEY_B, KEY_SPACE,
	KEY_MENU, KEY_RESERVED,
        // Row 6
        KEY_4, KEY_W, KEY_P, KEY_K, KEY_N, KEY_KATAKANA /* Font Size */,
	KEY_HIRAGANA /* Back/Undo */, KEY_RESERVED,
	// Row 7
	KEY_5, KEY_E, KEY_A, KEY_L, KEY_M, KEY_MUHENKAN /* SYM */,
	KEY_RESERVED, KEY_RESERVED,
     },

// keymapping[5] = key mapping for Nell EVT2 and later
// NOTE: 64 keys; even those unused
// NOTE: The number keys in Nell are combined with the first
//       row of letters (Q/1, W/2, etc).  We still need to define
//       the number key codes in the matrix so that they are enabled
//       for the input event driver.  Since we cannot define two key codes
//       in one row/col entry, these 10 keycodes are defined in unused
//       row/col entries.

     {
        // Row 0
	KEY_RESERVED, KEY_6, KEY_R, KEY_S, KEY_BACKSPACE, KEY_DOT,
	KEY_HIRAGANA /* Back */, KEY_RESERVED,
	// Row 1
	KEY_RESERVED, KEY_7, KEY_T, KEY_D, KEY_Z, KEY_SLASH,
	KEY_VOLUMEUP, KEY_RESERVED,
	// Row 2
	KEY_RESERVED, KEY_8, KEY_Y, KEY_F, KEY_X, KEY_ENTER,
	KEY_VOLUMEDOWN, KEY_RESERVED,
	// Row 3
	KEY_1, KEY_9, KEY_U, KEY_G, KEY_C, KEY_LEFTSHIFT,
	KEY_KPSLASH /* Home */, KEY_RESERVED,
	// Row 4
	KEY_2, KEY_0, KEY_I, KEY_H, KEY_V, KEY_LEFTALT,
	KEY_MENU, KEY_RESERVED,
	// Row 5
	KEY_3, KEY_Q, KEY_O, KEY_J, KEY_B, KEY_SPACE,
	KEY_YEN /* Next-Page right button */, KEY_RESERVED,
        // Row 6
        KEY_4, KEY_W, KEY_P, KEY_K, KEY_N, KEY_KATAKANA /* Font Size */,
	KEY_PAGEDOWN /* Prev-page */, KEY_RESERVED,
	// Row 7
	KEY_5, KEY_E, KEY_A, KEY_L, KEY_M, KEY_MUHENKAN /* SYM */,
	KEY_RESERVED, KEY_RESERVED,
     },

// NOTE: If you add more keypad mappings, remember to increment
//       NUMBER_OF_KB_MAPPINGS above

};

static struct resource mxc_kpp_resources[] = {
	[0] = {
	       .start = INT_KPP,
	       .end = INT_KPP,
	       .flags = IORESOURCE_IRQ,
	       }
};

static struct keypad_data evb_8_by_8_keypad = {
	.rowmax = 8,
	.colmax = 8,
	.irq = INT_KPP,
	.learning = 0,
	.delay = 2,
	.matrix = (u16 *)keymapping,
};

/* mxc keypad driver */
static struct platform_device mxc_keypad_device = {
	.name = "mxc_keypad",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_kpp_resources),
	.resource = mxc_kpp_resources,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &evb_8_by_8_keypad,
		},
};

static void mxc_init_keypad(void)
{
	(void)platform_device_register(&mxc_keypad_device);
}
#else
static inline void mxc_init_keypad(void)
{
}
#endif

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
/*!
 * The serial port definition structure. The fields contain:
 * {UART, CLK, PORT, IRQ, FLAGS}
 */
static struct plat_serial8250_port serial_platform_data[] = {
	{
	 .membase = (void __iomem *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTA),
	 .mapbase = (unsigned long)(CS4_BASE_ADDR + PBC_SC16C652_UARTA),
	 .irq = EXPIO_INT_XUART_INTA,
	 .uartclk = 14745600,
	 .regshift = 0,
	 .iotype = UPIO_MEM,
	 .flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	 },
	{
	 .membase = (void __iomem *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTB),
	 .mapbase = (unsigned long)(CS4_BASE_ADDR + PBC_SC16C652_UARTB),
	 .irq = EXPIO_INT_XUART_INTB,
	 .uartclk = 14745600,
	 .regshift = 0,
	 .iotype = UPIO_MEM,
	 .flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	 },
	{},
};

/*!
 * REVISIT: document me
 */
static struct platform_device serial_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = serial_platform_data,
		},
};

/*!
 * REVISIT: document me
 */
static int __init mxc_init_extuart(void)
{
	return platform_device_register(&serial_device);
}
#else
static inline int mxc_init_extuart(void)
{
	return 0;
}
#endif

/* MTD NOR flash */

#if defined(CONFIG_MTD_MXC) || defined(CONFIG_MTD_MXC_MODULE)

#ifdef CONFIG_MACH_MARIO_MX
#if 1
/* 8Mbyte version */
static struct mtd_partition mxc_nor_partitions[] = {
	{
	 .name = "Bootloader1",
	 .size = 128 * 1024,
	 .offset = 0x00020000,
	 .mask_flags = 0},
	{
	 .name = "Kernel1",
	 .size = 3584 * 1024,
	 .offset = 0x00060000,
	 .mask_flags = 0},
	{
	 .name = "BoardId",
	 .size = 32 * 1024,
	 .offset = 0x00008000,
	 .mask_flags = 0},
	{
	 .name = "Bootloader2",
	 .size = 128 * 1024,
	 .offset = 0x00040000,
	 .mask_flags = 0},
	{
	 .name = "Kernel2",
	 .size = 3584 * 1024,
	 .offset = 0x00400000,
	 .mask_flags = 0},
	{
	 .name = "BootEnv",
	 .size = 64 * 1024,
	 .offset = 0x00010000,
	 .mask_flags = 0},
	{
	 .name = "Diags",
	 .size = 128 * 1024,
	 .offset = 0x003e0000,
	 .mask_flags = 0},
	{
	 .name = "Vectors",
	 .size = 24 * 1024,
	 .offset = 0x00000000,
	 .mask_flags = 0}, /* one day we should force this as read-only */
	{
	 .name = "unused0",
	 .size = 64 * 1024,
	 .offset = 0x00010000,
	 .mask_flags = 0},
};
#else
/* 4Mbyte version */
static struct mtd_partition mxc_nor_partitions[] = {
	{
	 .name = "Bootloader1",
	 .size = 128 * 1024,
	 .offset = 0x00020000,
	 .mask_flags = 0},
	{
	 .name = "Kernel1",
	 .size = 1792 * 1024,
	 .offset = 0x00060000,
	 .mask_flags = 0},
	{
	 .name = "BoardId",
	 .size = 32 * 1024,
	 .offset = 0x00008000,
	 .mask_flags = 0},
	{
	 .name = "Bootloader2",
	 .size = 128 * 1024,
	 .offset = 0x00040000,
	 .mask_flags = 0},
	{
	 .name = "Kernel2",
	 .size = 1792 * 1024,
	 .offset = 0x00220000,
	 .mask_flags = 0},
	{
	 .name = "BootEnv",
	 .size = 64 * 1024,
	 .offset = 0x00010000,
	 .mask_flags = 0},
	{
	 .name = "Diags",
	 .size = 128 * 1024,
	 .offset = 0x003e0000,
	 .mask_flags = 0},
	{
	 .name = "Vectors",
	 .size = 24 * 1024,
	 .offset = 0x00000000,
	 .mask_flags = 0}, /* one day we should force this as read-only */
	{
	 .name = "unused0",
	 .size = 64 * 1024,
	 .offset = 0x00010000,
	 .mask_flags = 0},
};
#endif
#endif

static struct flash_platform_data mxc_flash_data = {
	.map_name = "cfi_probe",
	.width = 2,
	.parts = mxc_nor_partitions,
	.nr_parts = ARRAY_SIZE(mxc_nor_partitions),
};

static struct resource mxc_flash_resource = {
	.start = 0xa0000000,
	.end = 0xa0000000 + 0x00800000 - 1,
	.flags = IORESOURCE_MEM,

};

static struct platform_device mxc_nor_mtd_device = {
	.name = "mxc_nor_flash",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mxc_flash_data,
		},
	.num_resources = 1,
	.resource = &mxc_flash_resource,
};

static void mxc_init_nor_mtd(void)
{
	(void)platform_device_register(&mxc_nor_mtd_device);
}
#else
static void mxc_init_nor_mtd(void)
{
}
#endif

/* MTD NAND flash */

#if defined(CONFIG_MTD_NAND_MXC) || defined(CONFIG_MTD_NAND_MXC_MODULE)

static struct mtd_partition mxc_nand_partitions[4] = {
	{
	 .name = "IPL-SPL",
	 .offset = 0,
	 .size = 128 * 1024},
	{
	 .name = "nand.kernel",
	 .offset = MTDPART_OFS_APPEND,
	 .size = 4 * 1024 * 1024},
	{
	 .name = "nand.rootfs",
	 .offset = MTDPART_OFS_APPEND,
	 .size = 22 * 1024 * 1024},
	{
	 .name = "nand.userfs",
	 .offset = MTDPART_OFS_APPEND,
	 .size = MTDPART_SIZ_FULL},
};

static struct flash_platform_data mxc_nand_data = {
	.parts = mxc_nand_partitions,
	.nr_parts = ARRAY_SIZE(mxc_nand_partitions),
	.width = 1,
};

static struct platform_device mxc_nand_mtd_device = {
	.name = "mxc_nand_flash",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &mxc_nand_data,
		},
};

static void mxc_init_nand_mtd(void)
{
	if (__raw_readl(MXC_CCM_RCSR) & MXC_CCM_RCSR_NF16B) {
		mxc_nand_data.width = 2;
	}
	(void)platform_device_register(&mxc_nand_mtd_device);
}
#else
static inline void mxc_init_nand_mtd(void)
{
}
#endif

static struct spi_board_info mxc_spi_board_info[] __initdata = {
	{
	 .modalias = "pmic_spi",
	 .irq = IOMUX_TO_IRQ(MX31_PIN_GPIO1_3),
	 .max_speed_hz = 4000000,
	 .bus_num = 2,
	 .chip_select = 0,
	 },
};

#if defined(CONFIG_SMC911X) || defined(CONFIG_SMC911X_MODULE)
static struct resource smc911x_resources[] = {
	[0] = {
		.start	= 0xb4020000,
		.end	= 0xb4020000 + 0xffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IOMUX_TO_IRQ(MX31_PIN_ATA_CS1),
		.end	= IOMUX_TO_IRQ(MX31_PIN_ATA_CS1),
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device smc911x_device = {
	.name		= "smc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc911x_resources),
	.resource	= smc911x_resources,
};

static void mxc_init_smc911x(void)
{
	(void)platform_device_register(&smc911x_device);
}
#else
static void mxc_init_smc911x(void)
{
}


#endif 

#if defined(CONFIG_FB_MXC_SYNC_PANEL) || defined(CONFIG_FB_MXC_SYNC_PANEL_MODULE)
static const char fb_default_mode[] = "Sharp-QVGA";

/* mxc lcd driver */
static struct platform_device mxc_fb_device = {
	.name = "mxc_sdc_fb",
	.id = 0,
	.dev = {
		.release = mxc_nop_release,
		.platform_data = &fb_default_mode,
		.coherent_dma_mask = 0xFFFFFFFF,
		},
};

static void mxc_init_fb(void)
{
	(void)platform_device_register(&mxc_fb_device);
}
#else
static inline void mxc_init_fb(void)
{
}
#endif

/*
 * This may get called early from board specific init
 */
int mxc_expio_init(void)
{
    return 0;
}

/*!
 * Board specific fixup function. It is called by \b setup_arch() in
 * setup.c file very early on during kernel starts. It allows the user to
 * statically fill in the proper values for the passed-in parameters. None of
 * the parameters is used currently.
 *
 * @param  desc         pointer to \b struct \b machine_desc
 * @param  tags         pointer to \b struct \b tag
 * @param  cmdline      pointer to the command line
 * @param  mi           pointer to \b struct \b meminfo
 */
static void __init fixup_mxc_board(struct machine_desc *desc, struct tag *tags,
				   char **cmdline, struct meminfo *mi)
{
	mxc_cpu_init();
	
	// Tell the system that we only have one bank of contiguious RAM (bank 0)
	// so that we can reserve parts of it to persist across reboots.
	//
	mi->bank[0].start = PLFRM_MEM_BASE;
	mi->bank[0].size = PLFRM_MEM_SIZE;
	mi->nr_banks = 1;
}

static void __init mario_mxc_init_irq(void)
{
	// Note: We do this in the IRQ init prior to init'ing the IRQs so that
	// interrupts are still disabled prior to our changing the memory
	// layout.
	//
	reserve_bootmem(BOOT_GLOBALS_BASE, BOOT_GLOBALS_SIZE);
	reserve_bootmem(OOPS_SAVE_BASE, OOPS_SAVE_SIZE);
	
	mxc_init_irq();
}

extern void gpio_activate_audio_ports(void);

static void __init mxc_init_pmic_audio(void)
{
	struct clk *pll_clk;
	struct clk *ssi_clk;
	struct clk *ckih_clk;
	struct clk *cko_clk;
	unsigned int ccmr;

	/* Enable 26 mhz clock on CKO1 for PMIC audio */
	ckih_clk = clk_get(NULL, "ckih");
	cko_clk = clk_get(NULL, "cko1_clk");
	if (IS_ERR(ckih_clk) || IS_ERR(cko_clk)) {
		printk(KERN_ERR "Unable to set CKO1 output to CKIH\n");
	} else {
		clk_set_parent(cko_clk, ckih_clk);
		clk_set_rate(cko_clk, clk_get_rate(ckih_clk));
		clk_disable(cko_clk); /* Keep CLKO disabled by default */
	}
	clk_put(ckih_clk);
	clk_put(cko_clk);

	ccmr = __raw_readl(MXC_CCM_CCMR);
	ccmr &= 0xff93ffff;
	__raw_writel(ccmr, MXC_CCM_CCMR);

	udelay(10);

	/* Assign MCU PLL to be used by SSI1/2 */
	pll_clk = clk_get(NULL, "mcu_pll");
	ssi_clk = clk_get(NULL, "ssi_clk.0");
	clk_set_parent(ssi_clk, pll_clk);
	clk_enable(ssi_clk);
	clk_put(ssi_clk);

	ssi_clk = clk_get(NULL, "ssi_clk.1");
	clk_set_parent(ssi_clk, pll_clk);
	clk_enable(ssi_clk);
	clk_put(ssi_clk);
	clk_put(pll_clk);

	gpio_activate_audio_ports();
}

/*
 * mx31 power off (halt)
 */
static void mx31_pm_power_off(void)
{
	t_pc_config pc_config;

	pc_config.auto_en_vbkup2= 0;
	pc_config.en_vbkup2= 0;
	pc_config.vhold_voltage = 3;
	pc_config.vhold_voltage2 = 3;
	pc_config.auto_en_vbkup1 = 0;
	pc_config.en_vbkup1 = 0;
	pc_config.warm_enable = 0;
	pc_config.pc_enable = 1;
	pc_config.pc_timer = 5;
	pc_config.pc_count_enable = 1;
	pc_config.pc_count = 0;
	pc_config.pc_max_count = 7;
	pc_config.user_off_pc = 1;
	pmic_power_set_pc_config(&pc_config);
	mdelay(100);

	pmic_power_off(); /* Trigger power off - starts 8ms timer */

	return;
}

/*!
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	pm_power_off = mx31_pm_power_off;

	mxc_cpu_common_init();
	mxc_clocks_init();
	mxc_init_pmic_audio();
	mxc_gpio_init();
	mario_gpio_init();
	mxc_init_keypad();
	mxc_init_nor_mtd();
	mxc_init_nand_mtd();

	spi_register_board_info(mxc_spi_board_info,
				ARRAY_SIZE(mxc_spi_board_info));

	mxc_init_smc911x();

//	mxc_init_fb();
}

#define PLL_PCTL_REG(pd, mfd, mfi, mfn)		\
	((((pd) - 1) << 26) + (((mfd) - 1) << 16) + ((mfi)  << 10) + mfn)

/* For 26MHz input clock */
#define PLL_532MHZ		PLL_PCTL_REG(1, 13, 10, 3)
#define PLL_399MHZ		PLL_PCTL_REG(1, 52, 7, 35)
#define PLL_133MHZ		PLL_PCTL_REG(2, 26, 5, 3)

/* For 27MHz input clock */
#define PLL_532_8MHZ		PLL_PCTL_REG(1, 15, 9, 13)
#define PLL_399_6MHZ		PLL_PCTL_REG(1, 18, 7, 7)
#define PLL_133_2MHZ		PLL_PCTL_REG(3, 5, 7, 2)

#define PDR0_REG(mcu, max, hsp, ipg, nfc)	\
	(MXC_CCM_PDR0_MCU_DIV_##mcu | MXC_CCM_PDR0_MAX_DIV_##max | \
	 MXC_CCM_PDR0_HSP_DIV_##hsp | MXC_CCM_PDR0_IPG_DIV_##ipg | \
	 MXC_CCM_PDR0_NFC_DIV_##nfc)

/* working point(wp): 0 - 133MHz; 1 - 266MHz; 2 - 399MHz; 3 - 532MHz */
/* 26MHz input clock table */
static struct cpu_wp cpu_wp_26[] = {
	{
	 .pll_reg = PLL_532MHZ,
	 .pll_rate = 532000000,
	 .cpu_rate = 133000000,
	 .pdr0_reg = PDR0_REG(4, 4, 4, 2, 6),},
	{
	 .pll_reg = PLL_532MHZ,
	 .pll_rate = 532000000,
	 .cpu_rate = 266000000,
	 .pdr0_reg = PDR0_REG(2, 4, 4, 2, 6),},
	{
	 .pll_reg = PLL_399MHZ,
	 .pll_rate = 399000000,
	 .cpu_rate = 399000000,
	 .pdr0_reg = PDR0_REG(1, 3, 3, 2, 6),},
	{
	 .pll_reg = PLL_532MHZ,
	 .pll_rate = 532000000,
	 .cpu_rate = 532000000,
	 .pdr0_reg = PDR0_REG(1, 4, 4, 2, 6),},
};

/* 27MHz input clock table */
// static struct cpu_wp cpu_wp_27[] = {
// 	{
// 	 .pll_reg = PLL_532_8MHZ,
// 	 .pll_rate = 532800000,
// 	 .cpu_rate = 133200000,
// 	 .pdr0_reg = PDR0_REG(4, 4, 4, 2, 6),},
// 	{
// 	 .pll_reg = PLL_532_8MHZ,
// 	 .pll_rate = 532800000,
// 	 .cpu_rate = 266400000,
// 	 .pdr0_reg = PDR0_REG(2, 4, 4, 2, 6),},
// 	{
// 	 .pll_reg = PLL_399_6MHZ,
// 	 .pll_rate = 399600000,
// 	 .cpu_rate = 399600000,
// 	 .pdr0_reg = PDR0_REG(1, 3, 3, 2, 6),},
// 	{
// 	 .pll_reg = PLL_532_8MHZ,
// 	 .pll_rate = 532800000,
// 	 .cpu_rate = 532800000,
// 	 .pdr0_reg = PDR0_REG(1, 4, 4, 2, 6),},
// };

struct cpu_wp *get_cpu_wp(int *wp)
{
	*wp = 4;
	return cpu_wp_26;
}


/*
 * The following uses standard kernel macros define in arch.h in order to
 * initialize the __mach_desc_MARIO_MX data structure.
 */
/* *INDENT-OFF* */
MACHINE_START(MARIO_MX, "Mario Platform")
	/* Maintainer: Lab126, Inc. */
	.phys_io = AIPS1_BASE_ADDR,
	.io_pg_offst = ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params = PHYS_OFFSET + 0x100,
	.fixup = fixup_mxc_board,
	.map_io = mxc_map_io,
	.init_irq = mario_mxc_init_irq,
	.init_machine = mxc_board_init,
	.timer = &mxc_timer,
MACHINE_END
