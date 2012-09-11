/*
 *  linux/include/asm-arm/arch-pxa/fiona.h
 &  Ported from gumstix.h
 *
 *  Copyright:  (C) 2005, Lab126, Inc.  Nick Vaccaro <nvaccaro@lab126.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARCH_FIONA_H__
#define __ASM_ARCH_FIONA_H__

/*
 * Fiona Reboot Priority
 */
#define FIONA_REBOOT_BOOT_GLOBALS   0x00000000  // Do boot globals last.
#define FIONA_REBOOT_EINK_FB        0x3FFFFFFF  // Do display AFTER...
#define FIONA_REBOOT_PNLCD          0x7FFFFFFF  // ...PNLCD.

/*
 * Fiona Board Structure Information
 */
typedef struct fiona_hw {
    int sleep_switch_gpio;
    int sleep_switch_md;
    int sleep_switch_polarity;
    int sleep_switch_irq;
    int hold_switch_gpio;
    int hold_switch_md;
    int hold_switch_polarity;
    int hold_switch_irq;
    int wan_switch_gpio;
    int wan_switch_md;
    int wan_switch_polarity;
    int wan_switch_irq;
    int wake_on_wan_gpio;
    int wake_on_wan_md;
    int wake_on_wan_polarity;
    int wake_on_wan_irq;
    int ioc_wake_gpio;
    int ioc_wake_md;
    int ioc_reset_gpio;
    int ioc_reset_polarity;
    int ioc_flags;
    int pgsr0_reg;
    int pgsr1_reg;
    int pgsr2_reg;
    unsigned long wan_watermark;
    unsigned long low_watermark;
    unsigned long critical_watermark;
#ifdef CONFIG_SAFETY_NET
    unsigned long wan_safety_net;
    unsigned long low_safety_net;
    unsigned long critical_safety_net;
#endif
    int pwer;
    int pfer;
    int prer;
    int ioc_wake_polarity;
    int usb_wake_gpio;
    int usb_wake_polarity;
} fiona_hw;

// ioc_flags field accessors
//
#define IOC_CAN_WAKE_SINGLE_KEY_BIT (1 << 0)
#define IOC_CAN_WAKE_DUAL_KEY_BIT   (1 << 1)

extern fiona_hw g_fiona_hw;

/*
 * Fiona Board Revision Information
 */
extern int get_fiona_board_revision(void);


/* Fiona Board Version Information */
#define PHYS_BOARD_REV_ID   0x14000002  /* Board revision ID..*/
#define BOARD_REV_ID_MASK   0x0F        /* ..is in low nybble */
#define BOARD_REV_ID_P1     0x00
#define BOARD_REV_ID_P2     0x00
#define BOARD_REV_ID_P3     0x01
#define BOARD_REV_ID_P3_5   0x02
#define BOARD_REV_ID_EVT1   0x03
#define BOARD_REV_ID_EVT2   0x04
#define BOARD_REV_ID_EVT3   0x05
#define BOARD_REV_ID_DVT1   0x06
#define BOARD_REV_ID_REV2   0x07

#define BOARD_REV_ID_P4     0x00        /* Supposed to be 0x0F, but doesn't work reliably. */

/*
 * Translated Version Constants
 */
#define FIONA_BOARD_P4      16          /* P4   Board ID Linux system_rev value */
#define FIONA_BOARD_REV2    9           /* REV2 Board ID Linux system_rev value */
#define FIONA_BOARD_DVT1    8           /* DVT1 Board ID Linux system_rev value */
#define FIONA_BOARD_EVT3    7           /* EVT3 Board ID Linux system_rev value */
#define FIONA_BOARD_EVT2    6           /* EVT2 Board ID Linux system_rev value */
#define FIONA_BOARD_EVT1    5           /* EVT1 Board ID Linux system_rev value */
#define FIONA_BOARD_P3_5    4           /* P3.5 Board ID Linux system_rev value */
#define FIONA_BOARD_P3      3           /* P3   Board ID Linux system_rev value */
#define FIONA_BOARD_P2      2           /* P2   Board ID Linux system_rev value */

/* GPIOn - Input from MAX823 (or equiv), normalizing USB +5V 
	 into a clean interrupt signal for determining cable presence 
	 On the original gumstix, this is GPIO81, and GPIO83 needs to be defined as well.
	 On the gumstix F, this moves to GPIO17 and GPIO37 */
/* GPIOx - Connects to USB D+ and used as a pull-up after GPIOn 
	 has detected a cable insertion; driven low otherwise. */


#define GPIO_FIONA_USB_GPIOn			81	 
#define GPIO_FIONA_USB_GPIOx			83	 

#define FIONA_USB_INTR_IRQ			IRQ_GPIO(GPIO_FIONA_USB_GPIOn)	/* usb state change */
#define GPIO_FIONA_USB_GPIOn_MD			(GPIO_FIONA_USB_GPIOn | GPIO_IN)
#define GPIO_FIONA_USB_GPIOx_CON_MD		(GPIO_FIONA_USB_GPIOx | GPIO_OUT)
#define GPIO_FIONA_USB_GPIOx_DIS_MD		(GPIO_FIONA_USB_GPIOx | GPIO_IN)

#define GPIO_FIONA_VC_TEST_MD   (22 | GPIO_OUT)


/*
 * Defines time that the system must hold off sending the next critical battery
 * event when detecting battery has passed critical watermark
 */
#define CRIT_BATT_NOTIFY_OSCR_DELTA     (OSCR_SECONDS * 10)

/*
 * SMC Ethernet definitions
 * ETH_RST provides a hardware reset line to the ethernet chip
 * ETH is the IRQ line in from the ethernet chip to the PXA
 */
#define GPIO_FIONA_ETH_RST				80
#define GPIO_FIONA_ETH_RST_MD			(GPIO_FIONA_ETH_RST | GPIO_OUT)

#define GPIO_FIONA_ETH					36
#define GPIO_FIONA_ETH_MD				(GPIO_FIONA_ETH | GPIO_IN)
#define FIONA_ETH_IRQ					IRQ_GPIO(GPIO_FIONA_ETH)

// The serial send and rx timeouts
#define OSCR_USECONDS       4                           // 4 OSCR ticks/uSec
#define OSCR_MSECONDS       (1000*OSCR_USECONDS)        // 1000 uSec/Sec
#define OSCR_SECONDS        (1000000*OSCR_USECONDS)     // 1000000 uSec/Sec


#ifdef CONFIG_IOC_VOLTAGE_CHECKER

  #define USE_ADJUSTED_BATTERY_INFO

  #define FIONA_WAN_SHUTDOWN_WATERMARK_REV2          3700
  #define FIONA_LOW_WATERMARK_REV2                   3600
  #define FIONA_CRITICAL_WATERMARK_REV2              3560

  #define FIONA_WAN_SHUTDOWN_WATERMARK_DVT1          3700
  #define FIONA_LOW_WATERMARK_DVT1                   3600
  #define FIONA_CRITICAL_WATERMARK_DVT1              3560

  #define FIONA_WAN_SHUTDOWN_WATERMARK_EVT3          3700
  #define FIONA_LOW_WATERMARK_EVT3                   3680
  #define FIONA_CRITICAL_WATERMARK_EVT3              3600

#else

  #undef USE_ADJUSTED_BATTERY_INFO

  // Low-battery watermarks, in mVolts
  #define FIONA_WAN_SHUTDOWN_WATERMARK_REV2          3700
  #define FIONA_LOW_WATERMARK_REV2                   3680
  #define FIONA_CRITICAL_WATERMARK_REV2              3600

  #define FIONA_WAN_SHUTDOWN_WATERMARK_DVT1          3700
  #define FIONA_LOW_WATERMARK_DVT1                   3680
  #define FIONA_CRITICAL_WATERMARK_DVT1              3600

  #define FIONA_WAN_SHUTDOWN_WATERMARK_EVT3          3700
  #define FIONA_LOW_WATERMARK_EVT3                   3680
  #define FIONA_CRITICAL_WATERMARK_EVT3              3600
#endif

// Watermark and voltage calculation constants
#define SAFETY_NET_HEIGHT_MVOLTS                    30     // Set safety net 30 mvolts above HW limit
#define MAX_MAMPS_WAN_MODE                          1300
#define MAX_MAMPS_NON_WAN_MODE                      600
#define HW_VOLTAGE_SHUTOFF_LEVEL                    3300    // Pretend HW is here to be conservative
#define REAL_HW_VOLTAGE_SHUTOFF_LEVEL               3270    // This is level the HW is really set at
#define WATERMARK_OP_AMP_CONSERVATIVE_FACTOR_MV     30      // Soaks up op amp offset error
#define WATERMARK_CONSERVATIVE_UPDATE_FACTOR_MV		20		// = calculated wan level + 20 or 3720
#define WATERMARK_CONSERVATIVE_LOW_FACTOR_MV        50      // Set low 50 mv above critical...
#define WATERMARK_CONSERVATIVE_CRITICAL_FACTOR_MV   80      // Set critical 80 mv above calculated safe zone...
#define RESISTANCE_CALCULATION_SAMPLE_COUNT         10

#define MAX_BOARD_RESISTANCE_GAIN                   30
#define MIN_BOARD_RESISTANCE                        180

// When reporting voltage to the battery icon (via /proc/ioc/battery),
// report back the battery voltage for the icon, which is based off
// of a different default board resistance value (200) than the watermark
// voltage check mechanism, which bases it off of the true Minimum board
// resistance value
//
// Sam wants to use a typical (not MIN) value of board resistance when
// doing Ohm's law adjustment for both watermarks and icon
#define TYPICAL_BOARD_RESISTANCE                    230
#define BATTERY_ICON_BOARD_RESISTANCE               (TYPICAL_BOARD_RESISTANCE)


// Time between generating watermark events of like kind
//
#define MIN_WATERMARK_SECS                      10
#define MIN_WATERMARK_JIFF_TIMEOUT              (MIN_WATERMARK_SECS * HZ)


//
// Since we're using a default board resistance, we have to make sure watermarks
// are set conservatively to handle resistance gain of the battery over time and
// still have the units function reliably.  WATERMARK_CONSERVATIVE_RESISTANCE_GAIN 
// is added to reported board resistance when determining actual watermark settings.
// By doing this, we sacrifice a bit of battery life in trade for boards not crashing
// at some point in the future as their battery gains resistance.
//
#define WATERMARK_CONSERVATIVE_RESISTANCE_GAIN      MAX_BOARD_RESISTANCE_GAIN

#define MAX_BOARD_RESISTANCE                        (MIN_BOARD_RESISTANCE+MAX_BOARD_RESISTANCE_GAIN)
#define WAN_SAFETY_NET_MVOLTS                       (REAL_HW_VOLTAGE_SHUTOFF_LEVEL + SAFETY_NET_HEIGHT_MVOLTS)
#define LOW_SAFETY_NET_MVOLTS                       (REAL_HW_VOLTAGE_SHUTOFF_LEVEL + SAFETY_NET_HEIGHT_MVOLTS)
#define CRITICAL_SAFETY_NET_MVOLTS                  (REAL_HW_VOLTAGE_SHUTOFF_LEVEL + SAFETY_NET_HEIGHT_MVOLTS)

#define CALC_WATERMARK_UPDATE(w)					(MAX(3720,(WATERMARK_CONSERVATIVE_UPDATE_FACTOR_MV+w)))

/*
 * The following are missing from pxa-regs.h
 */

#define GPIO0_POWER						0
#define GPIO4_nBVD1						4
#define GPIO4_nSTSCHG					GPIO4_nBVD1
#define GPIO8_RESET						8			// On P2 boards
#define GPIO9_SD_WP						9			// On P3 and newer boards
#define GPIO11_nPCD1					11
#define GPIO22_nINPACK					22
#define GPIO26_PRDY_nBSY0				26
#define GPIO36_nBVD2					36
#define GPIO52_AUD_OUT_POWER			52			// On REV2 and newer boards
#define GPIO57_HSON						57
#define GPIO72_HPDET                    72
#define GPIO82_nAPOLLO_SHDN             82

#define GPIO0_POWER_MD					( GPIO0_POWER | GPIO_IN )
#define GPIO4_nBVD1_MD					( GPIO4_nBVD1| GPIO_IN )
#define GPIO4_nSTSCHG_MD				( GPIO4_nSTSCHG | GPIO_IN )
#define GPIO8_RESET_MD					( GPIO8_RESET | GPIO_OUT )
#define GPIO9_SD_WP_MD					( GPIO9_SD_WP | GPIO_IN )
#define GPIO11_nPCD1_MD					( GPIO11_nPCD1 | GPIO_IN )
#define GPIO22_nINPACK_MD				( GPIO22_nINPACK | GPIO_IN )
#define GPIO26_PRDY_nBSY0_MD			( GPIO26_PRDY_nBSY0 | GPIO_IN )
#define GPIO36_nBVD2_MD					( GPIO36_nBVD2 | GPIO_IN )

// FFUART lines to the WAN
#define GPIO39_FIONA_FFTXD_MD			( GPIO39_FFTXD | GPIO_OUT | GPIO_DFLT_LOW )
#define GPIO40_FIONA_FFDTR_MD			( GPIO40_FFDTR | GPIO_OUT | GPIO_DFLT_LOW )
#define GPIO41_FIONA_FFRTS_MD			( GPIO41_FFRTS | GPIO_OUT | GPIO_DFLT_LOW )


#define FIONA_nSTSCHG_IRQ				IRQ_GPIO(GPIO4_nSTSCHG)
#define FIONA_nPCD1_IRQ					IRQ_GPIO(GPIO11_nPCD1)
#define FIONA_nBVD1_IRQ					IRQ_GPIO(GPIO4_nBVD1)
#define FIONA_nBVD2_IRQ					IRQ_GPIO(GPIO36_nBVD2)
#define FIONA_PRDY_nBSY0_IRQ			IRQ_GPIO(GPIO26_PRDY_nBSY0)
#define FIONA_HPDET_IRQ					IRQ_GPIO(GPIO72_HPDET)

// Apollo GPIOs
#define GPIO66_FIONA_APOLLO_H_DS        66
#define GPIO70_FIONA_APOLLO_H_WUP       70
#define GPIO83_FIONA_APOLLO_H_ACK       83

// P3 Board Constants
#define GPIO0_FIONA_P3_POWER			0
#define GPIO4_FIONA_P3_HOST_WAKE		4
#define GPIO8_FIONA_P3_RESET			8
#define GPIO9_FIONA_P3_SD_WP			9 
#define GPIO11_FIONA_P3_nPCD1			11
#define GPIO14_FIONA_P3_WAN_EN			14
#define GPIO22_FIONA_P3_nINPACK			22
#define GPIO24_FIONA_P3_WAN_ON_OFF		24
#define GPIO26_FIONA_P3_PRDY_nBSY0		26
#define GPIO36_FIONA_P3_nBVD2			36
#define GPIO45_FIONA_P3_SD_CD			45
#define GPIO81_FIONA_P3_MODULE_WAKE		81

#define GPIO_FIONA_P3_IOC_RESET         71  // Doesn't exist on 3.5, spoof with test point
#define GPIO_FIONA_P3_IOC_RESET_MD    ( GPIO_FIONA_P3_IOC_RESET | GPIO_OUT )
#define FIONA_P3_IOC_RESET_POLARITY   0

#define GPIO0_FIONA_P3_POWER_MD			( GPIO0_FIONA_P3_POWER | GPIO_IN )
#define	GPIO4_FIONA_P3_HOST_WAKE_MD		( GPIO4_FIONA_P3_HOST_WAKE | GPIO_IN )
#define GPIO8_FIONA_P3_RESET_MD			( GPIO8_FIONA_P3_RESET | GPIO_OUT )
#define GPIO9_FIONA_P3_SD_WP_MD			( GPIO9_FIONA_P3_SD_WP | GPIO_IN )
#define	GPIO14_FIONA_P3_WAN_EN_MD		( GPIO14_FIONA_P3_WAN_EN | GPIO_IN )
#define GPIO11_FIONA_P3_nPCD1_MD		( GPIO11_FIONA_P3_nPCD1 | GPIO_IN )
#define GPIO22_FIONA_P3_nINPACK_MD		( GPIO22_FIONA_P3_nINPACK | GPIO_IN )
#define GPIO24_FIONA_P3_WAN_ON_OFF_MD	( GPIO24_FIONA_P3_WAN_ON_OFF | GPIO_OUT )
#define GPIO26_FIONA_P3_PRDY_nBSY0_MD	( GPIO26_FIONA_P3_PRDY_nBSY0 | GPIO_IN )
#define GPIO36_FIONA_P3_nBVD2_MD		( GPIO36_FIONA_P3_nBVD2 | GPIO_IN )
#define GPIO45_FIONA_P3_SD_CD_MD		( GPIO45_FIONA_P3_SD_CD | GPIO_IN ) 
#define	GPIO81_FIONA_P3_MODULE_WAKE_MD	( GPIO81_FIONA_P3_MODULE_WAKE | GPIO_OUT )

#define FIONA_P3_HOST_WAKE_IRQ			IRQ_GPIO(GPIO4_FIONA_P3_HOST_WAKE)
#define FIONA_P3_nPCD1_IRQ				IRQ_GPIO(GPIO11_FIONA_P3_nPCD1)
#define FIONA_P3_WAN_EN_IRQ				IRQ_GPIO(GPIO14_FIONA_P3_WAN_EN)
#define FIONA_P3_nBVD2_IRQ				IRQ_GPIO(GPIO36_FIONA_P3_nBVD2)
#define FIONA_P3_PRDY_nBSY0_IRQ			IRQ_GPIO(GPIO26_FIONA_P3_PRDY_nBSY0)
#define FIONA_P3_SD_CD_IRQ				IRQ_GPIO(GPIO45_FIONA_P3_SD_CD)

#define GPIO_FIONA_P3_HOLD_SWITCH         0
#define GPIO_FIONA_P3_HOLD_SWITCH_MD      ( GPIO_FIONA_P3_HOLD_SWITCH | GPIO_IN )
#define FIONA_P3_HOLD_SWITCH_INTR_IRQ     IRQ_GPIO(GPIO_FIONA_P3_HOLD_SWITCH)
#define FIONA_P3_HOLD_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_P3_POWER_SWITCH        1
#define GPIO_FIONA_P3_POWER_SWITCH_MD     (GPIO_FIONA_P3_POWER_SWITCH | GPIO_IN)
#define FIONA_P3_POWER_SWITCH_INTR_IRQ    IRQ_GPIO(GPIO_FIONA_P3_POWER_SWITCH)
#define FIONA_P3_POWER_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_P3_SLEEP_SWITCH        GPIO_FIONA_P3_POWER_SWITCH
#define GPIO_FIONA_P3_SLEEP_SWITCH_MD          GPIO_FIONA_P3_POWER_SWITCH_MD
#define FIONA_P3_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_P3_POWER_SWITCH_ASSERT_POLARITY
#define FIONA_P3_SLEEP_SWITCH_INTR_IRQ          IRQ_GPIO(GPIO_FIONA_P3_SLEEP_SWITCH)

/*
 * Define the desired states of GPIO's during sleep
 * NOTE: This is duplicated in bootloader tree: u-boot/configs/fiona.h
 * Define a constant for each board rev...
 */
#define CFG_FIONA_P3_PGSR0              0xFFFCFEFF
#define CFG_FIONA_P3_PGSR1				0xFCD7FFF7
#define CFG_FIONA_P3_PGSR2				0x001FFFFF

/*
 * OneNAND-related definitions.
 */
#define FIONA_ONENAND_ASYNC_PHYS_REG	(*(volatile unsigned short *)((unsigned int) 0x0001E442 + (unsigned int) UNCACHED_PHYS_0))
#define FIONA_ONENAND_ASYNC_REG			(*(volatile unsigned short *)0x0001E442)
#define FIONA_ONENAND_ASYNC_SET			0x40C0

#define FIONA_ONENAND_DBS_PHYS_REG		(*(volatile unsigned short *)((unsigned int) 0x0001E202 + (unsigned int) UNCACHED_PHYS_0))
#define FIONA_ONENAND_DBS_REG			(*(volatile unsigned short *)0x0001E202)
#define FIONA_ONENAND_DBS_CLEAR			0x0000

extern void fiona_set_onenand_sync(void);
extern void fiona_set_onenand_async(void);
extern void fiona_reset_onenand_dbs_bit(void);
extern void fiona_reset_onenand_dbs_bit_phys(void);

extern unsigned int board_resistance;

/*
 * MMC/SD-related definitions.
 */
#define mmc_query_insertion_state()	((GPLR(GPIO45_FIONA_P3_SD_CD) & GPIO_bit(GPIO45_FIONA_P3_SD_CD)) ? 0 : 1)
#define mmc_query_readonly_state()	((GPLR(GPIO9_SD_WP) & GPIO_bit(GPIO9_SD_WP)) ? 1 : 0)


/*************************************************************************
 *
 * For EVT1
 *
 *************************************************************************/

#define GPIO_FIONA_EVT1_WAKE_ON_WAN         4
#define GPIO_FIONA_EVT1_WAKE_ON_WAN_MD      (GPIO_FIONA_EVT1_WAKE_ON_WAN | GPIO_IN) 
#define FIONA_EVT1_WAKE_ON_WAN_ON_POLARITY  1
#define FIONA_EVT1_WAKE_ON_WAN_INTR_IRQ     IRQ_GPIO(GPIO_FIONA_EVT1_WAKE_ON_WAN) 

#define GPIO_FIONA_EVT1_WAN_SWITCH          14
#define GPIO_FIONA_EVT1_WAN_SWITCH_MD       (GPIO_FIONA_EVT1_WAN_SWITCH | GPIO_IN) 
#define FIONA_EVT1_WAN_SWITCH_ON_POLARITY   1
#define FIONA_EVT1_WAN_SWITCH_INTR_IRQ      IRQ_GPIO(GPIO_FIONA_EVT1_WAN_SWITCH) 

#define GPIO_FIONA_EVT1_IOC_RESET       73
#define GPIO_FIONA_EVT1_IOC_RESET_MD    ( GPIO_FIONA_EVT1_IOC_RESET | GPIO_OUT )
#define FIONA_EVT1_IOC_RESET_POLARITY   0


#define GPIO_FIONA_EVT1_HOLD_SWITCH         0
#define GPIO_FIONA_EVT1_HOLD_SWITCH_MD      ( GPIO_FIONA_EVT1_HOLD_SWITCH | GPIO_IN )
#define FIONA_EVT1_HOLD_SWITCH_INTR_IRQ	    IRQ_GPIO(GPIO_FIONA_EVT1_HOLD_SWITCH)
#define FIONA_EVT1_HOLD_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT1_POWER_SWITCH        1
#define GPIO_FIONA_EVT1_POWER_SWITCH_MD     (GPIO_FIONA_EVT1_POWER_SWITCH | GPIO_IN)
#define FIONA_EVT1_POWER_SWITCH_INTR_IRQ    IRQ_GPIO(GPIO_FIONA_EVT1_POWER_SWITCH)
#define FIONA_EVT1_POWER_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT1_SLEEP_SWITCH        GPIO_FIONA_EVT1_POWER_SWITCH
#define GPIO_FIONA_EVT1_SLEEP_SWITCH_MD          GPIO_FIONA_EVT1_POWER_SWITCH_MD
#define FIONA_EVT1_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_EVT1_POWER_SWITCH_ASSERT_POLARITY
#define FIONA_EVT1_SLEEP_SWITCH_INTR_IRQ		  IRQ_GPIO(GPIO_FIONA_EVT1_SLEEP_SWITCH)

#define CFG_FIONA_EVT1_PGSR0			0xFFFDFEBF
#define CFG_FIONA_EVT1_PGSR1			0xFC96FFF7
#define CFG_FIONA_EVT1_PGSR2			0x001FFFFF


/*************************************************************************
 *
 * For EVT2
 *
 *************************************************************************/

#define GPIO_FIONA_EVT2_WAKE_ON_WAN         4
#define GPIO_FIONA_EVT2_WAKE_ON_WAN_MD      (GPIO_FIONA_EVT2_WAKE_ON_WAN | GPIO_IN) 
#define FIONA_EVT2_WAKE_ON_WAN_ON_POLARITY  1
#define FIONA_EVT2_WAKE_ON_WAN_INTR_IRQ     IRQ_GPIO(GPIO_FIONA_EVT2_WAKE_ON_WAN) 

#define GPIO_FIONA_EVT2_WAN_SWITCH          14
#define GPIO_FIONA_EVT2_WAN_SWITCH_MD       (GPIO_FIONA_EVT2_WAN_SWITCH | GPIO_IN) 
#define FIONA_EVT2_WAN_SWITCH_ON_POLARITY   1
#define FIONA_EVT2_WAN_SWITCH_INTR_IRQ      IRQ_GPIO(GPIO_FIONA_EVT2_WAN_SWITCH) 

#define GPIO_FIONA_EVT2_IOC_RESET           48
#define GPIO_FIONA_EVT2_IOC_RESET_MD        ( GPIO_FIONA_EVT2_IOC_RESET | GPIO_OUT )
#define FIONA_EVT2_IOC_RESET_POLARITY       0


#define GPIO_FIONA_EVT2_HOLD_SWITCH         0
#define GPIO_FIONA_EVT2_HOLD_SWITCH_MD      ( GPIO_FIONA_EVT2_HOLD_SWITCH | GPIO_IN )
#define FIONA_EVT2_HOLD_SWITCH_INTR_IRQ	    IRQ_GPIO(GPIO_FIONA_EVT2_HOLD_SWITCH)
#define FIONA_EVT2_HOLD_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT2_POWER_SWITCH        1
#define GPIO_FIONA_EVT2_POWER_SWITCH_MD     (GPIO_FIONA_EVT2_POWER_SWITCH | GPIO_IN)
#define FIONA_EVT2_POWER_SWITCH_INTR_IRQ    IRQ_GPIO(GPIO_FIONA_EVT2_POWER_SWITCH)
#define FIONA_EVT2_POWER_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT2_SLEEP_SWITCH        GPIO_FIONA_EVT2_POWER_SWITCH
#define GPIO_FIONA_EVT2_SLEEP_SWITCH_MD          GPIO_FIONA_EVT2_POWER_SWITCH_MD
#define FIONA_EVT2_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_EVT2_POWER_SWITCH_ASSERT_POLARITY
#define FIONA_EVT2_SLEEP_SWITCH_INTR_IRQ		  IRQ_GPIO(GPIO_FIONA_EVT2_SLEEP_SWITCH)

#define CFG_FIONA_EVT2_PGSR0				0xFFFDFEBF
#define CFG_FIONA_EVT2_PGSR1				0xFD97FFF7
#define CFG_FIONA_EVT2_PGSR2				0x001FFFFF


/*************************************************************************
 *
 * For EVT3
 *
 *************************************************************************/
#define GPIO_FIONA_EVT3_WAKE_ON_WAN         4
#define GPIO_FIONA_EVT3_WAKE_ON_WAN_MD      (GPIO_FIONA_EVT3_WAKE_ON_WAN | GPIO_IN) 
#define FIONA_EVT3_WAKE_ON_WAN_ON_POLARITY  1
#define FIONA_EVT3_WAKE_ON_WAN_INTR_IRQ     IRQ_GPIO(GPIO_FIONA_EVT3_WAKE_ON_WAN) 

#define GPIO_FIONA_EVT3_USB_WAKE            3
#define GPIO_FIONA_EVT3_USB_WAKE_POLARITY   0

#define GPIO_FIONA_EVT3_IOC_WAKE        5
#define GPIO_FIONA_EVT3_IOC_WAKE_MD     ( GPIO_FIONA_EVT3_IOC_WAKE | GPIO_IN )
#define FIONA_EVT3_IOC_WAKE_POLARITY    0

#define GPIO_FIONA_EVT3_WAN_SWITCH          14
#define GPIO_FIONA_EVT3_WAN_SWITCH_MD       (GPIO_FIONA_EVT3_WAN_SWITCH | GPIO_IN) 
#define FIONA_EVT3_WAN_SWITCH_ON_POLARITY   1
#define FIONA_EVT3_WAN_SWITCH_INTR_IRQ      IRQ_GPIO(GPIO_FIONA_EVT3_WAN_SWITCH) 

#define GPIO_FIONA_EVT3_IOC_RESET       48
#define GPIO_FIONA_EVT3_IOC_RESET_MD    ( GPIO_FIONA_EVT3_IOC_RESET | GPIO_OUT )
#define FIONA_EVT3_IOC_RESET_POLARITY   0

#define GPIO_FIONA_HPDET_EVT3			72
#define GPIO_FIONA_HPDET_EVT3_MD		( GPIO_FIONA_HPDET_EVT3 | GPIO_IN )
#define FIONA_EVT3_HPDET_IRQ			IRQ_GPIO(GPIO_FIONA_HPDET_EVT3)

#define GPIO_FIONA_EVT3_HOLD_SWITCH         13
#define GPIO_FIONA_EVT3_HOLD_SWITCH_MD      ( GPIO_FIONA_EVT3_HOLD_SWITCH | GPIO_IN )
#define FIONA_EVT3_HOLD_SWITCH_INTR_IRQ	    IRQ_GPIO(GPIO_FIONA_EVT3_HOLD_SWITCH)
#define FIONA_EVT3_HOLD_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT3_POWER_SWITCH        0
#define GPIO_FIONA_EVT3_POWER_SWITCH_MD     (GPIO_FIONA_EVT3_POWER_SWITCH | GPIO_IN)
#define FIONA_EVT3_POWER_SWITCH_INTR_IRQ    IRQ_GPIO(GPIO_FIONA_EVT3_POWER_SWITCH)
#define FIONA_EVT3_POWER_SWITCH_ASSERT_POLARITY   0

#define GPIO_FIONA_EVT3_SLEEP_SWITCH        GPIO_FIONA_EVT3_POWER_SWITCH
#define GPIO_FIONA_EVT3_SLEEP_SWITCH_MD          GPIO_FIONA_EVT3_POWER_SWITCH_MD
#define FIONA_EVT3_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_EVT3_POWER_SWITCH_ASSERT_POLARITY
#define FIONA_EVT3_SLEEP_SWITCH_INTR_IRQ		  IRQ_GPIO(GPIO_FIONA_EVT3_SLEEP_SWITCH)

// Fiona Wake Sources
#define CFG_FIONA_EVT3_PWER                     0x80000003
#define CFG_FIONA_EVT3_PRER                     0x00000003
#define CFG_FIONA_EVT3_PFER                     0x00000003

#define CFG_FIONA_EVT3_PGSR0				0xFEFFFEFF
#define CFG_FIONA_EVT3_PGSR1				0x01FFFC7F
#define CFG_FIONA_EVT3_PGSR2				0x001DCF80


/*************************************************************************
 *
 * For DVT1
 *
 *************************************************************************/
// Power Switch
#define GPIO_FIONA_DVT1_POWER_SWITCH        	0
#define GPIO_FIONA_DVT1_POWER_SWITCH_MD			(GPIO_FIONA_DVT1_POWER_SWITCH | GPIO_IN)
#define FIONA_DVT1_POWER_SWITCH_INTR_IRQ		IRQ_GPIO(GPIO_FIONA_DVT1_POWER_SWITCH)
#define FIONA_DVT1_POWER_SWITCH_ON_POLARITY		0
#define FIONA_DVT1_POWER_SWITCH_OFF_POLARITY	1

// Sleep Switch Definition
#define GPIO_FIONA_DVT1_SLEEP_SWITCH			 GPIO_FIONA_DVT1_POWER_SWITCH
#define GPIO_FIONA_DVT1_SLEEP_SWITCH_MD          GPIO_FIONA_DVT1_POWER_SWITCH_MD
#define FIONA_DVT1_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_DVT1_POWER_SWITCH_OFF_POLARITY
#define FIONA_DVT1_SLEEP_SWITCH_INTR_IRQ		 IRQ_GPIO(GPIO_FIONA_DVT1_SLEEP_SWITCH)

// IOC Wake Source (IOC's signal line to wake PXA when sleeping)
#define GPIO_FIONA_DVT1_IOC_WAKE        		1
#define GPIO_FIONA_DVT1_IOC_WAKE_MD     		( GPIO_FIONA_DVT1_IOC_WAKE | GPIO_IN )
#define FIONA_DVT1_IOC_WAKE_POLARITY    		0

// USB Wake Sources
#define GPIO_FIONA_DVT1_USB_WAKE                3
#define GPIO_FIONA_DVT1_USB_WAKE_POLARITY       0

// WAN Wake Source (WAN's signal line to wake PXA on ring)
#define GPIO_FIONA_DVT1_WAKE_ON_WAN             4
#define GPIO_FIONA_DVT1_WAKE_ON_WAN_MD          (GPIO_FIONA_DVT1_WAKE_ON_WAN | GPIO_IN) 
#define FIONA_DVT1_WAKE_ON_WAN_ON_POLARITY      1
#define FIONA_DVT1_WAKE_ON_WAN_INTR_IRQ         IRQ_GPIO(GPIO_FIONA_DVT1_WAKE_ON_WAN) 

#define GPIO_FIONA_DVT1_WAN_SWITCH              14
#define GPIO_FIONA_DVT1_WAN_SWITCH_MD           (GPIO_FIONA_DVT1_WAN_SWITCH | GPIO_IN) 
#define FIONA_DVT1_WAN_SWITCH_ON_POLARITY       1
#define FIONA_DVT1_WAN_SWITCH_INTR_IRQ          IRQ_GPIO(GPIO_FIONA_DVT1_WAN_SWITCH) 

// IOC Reset Line for resetting IOC
#define GPIO_FIONA_DVT1_IOC_RESET               48
#define GPIO_FIONA_DVT1_IOC_RESET_MD            ( GPIO_FIONA_DVT1_IOC_RESET | GPIO_OUT | GPIO_DFLT_HIGH )
#define FIONA_DVT1_IOC_RESET_POLARITY           0

// EINK Temperature Sensor
#define GPIO_FIONA_TEMP_SENSOR                  51
#define GPIO_FIONA_TEMP_SENSOR_MD               ( GPIO_FIONA_TEMP_SENSOR | GPIO_OUT | GPIO_DFLT_LOW )
#define FIONA_DVT1_TEMP_SENSOR_POLARITY         0

#define GPIO_FIONA_APOLLO_COMMON                80
#define GPIO_FIONA_APOLLO_COMMON_MD             ( GPIO_FIONA_APOLLO_COMMON | GPIO_OUT | GPIO_DFLT_LOW )

#define GPIO_FIONA_APOLLO_POWER                 82
#define GPIO_FIONA_APOLLO_POWER_MD              ( GPIO_FIONA_APOLLO_POWER | GPIO_OUT | GPIO_DFLT_HIGH )

// Headphone Detect Line
#define GPIO_FIONA_HPDET_DVT1					72
#define GPIO_FIONA_HPDET_DVT1_MD				( GPIO_FIONA_HPDET_DVT1 | GPIO_IN )
#define FIONA_DVT1_HPDET_IRQ					IRQ_GPIO(GPIO_FIONA_HPDET_DVT1)

// Fiona Wake Sources
// On/Off Switch (0)
// IOC Wake (1)
// USB_DC   (3)
// WAN Wake (4)
// WAN Enable Switch (14)
#define CFG_FIONA_DVT1_PWER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_DVT1_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_DVT1_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_DVT1_USB_WAKE) |  \
                                                (1 << GPIO_FIONA_DVT1_WAKE_ON_WAN) |  \
                                                (1 << GPIO_FIONA_DVT1_WAN_SWITCH))

#define CFG_FIONA_DVT1_PRER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_DVT1_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_DVT1_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_DVT1_USB_WAKE))

#define CFG_FIONA_DVT1_PFER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_DVT1_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_DVT1_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_DVT1_USB_WAKE))

// GPIO settings during sleep (if output and set to 1 here, GPIO driven high during sleep)
#define CFG_FIONA_DVT1_PGSR0					0xFEFFFEAF
#define CFG_FIONA_DVT1_PGSR1					0x01EFFC7F
#define CFG_FIONA_DVT1_PGSR2					0x0018FF90


/*************************************************************************
 *
 * For REV2
 *
 *************************************************************************/
// Power Switch
#define GPIO_FIONA_REV2_POWER_SWITCH        	0
#define GPIO_FIONA_REV2_POWER_SWITCH_MD			(GPIO_FIONA_REV2_POWER_SWITCH | GPIO_IN)
#define FIONA_REV2_POWER_SWITCH_INTR_IRQ		IRQ_GPIO(GPIO_FIONA_REV2_POWER_SWITCH)
#define FIONA_REV2_POWER_SWITCH_ON_POLARITY		0
#define FIONA_REV2_POWER_SWITCH_OFF_POLARITY	1

// Sleep Switch Definition
#define GPIO_FIONA_REV2_SLEEP_SWITCH			 GPIO_FIONA_REV2_POWER_SWITCH
#define GPIO_FIONA_REV2_SLEEP_SWITCH_MD          GPIO_FIONA_REV2_POWER_SWITCH_MD
#define FIONA_REV2_SLEEP_SWITCH_SLEEP_POLARITY   FIONA_REV2_POWER_SWITCH_OFF_POLARITY
#define FIONA_REV2_SLEEP_SWITCH_INTR_IRQ		 IRQ_GPIO(GPIO_FIONA_REV2_SLEEP_SWITCH)

// IOC Wake Source (IOC's signal line to wake PXA when sleeping)
#define GPIO_FIONA_REV2_IOC_WAKE        		1
#define GPIO_FIONA_REV2_IOC_WAKE_MD     		(GPIO_FIONA_REV2_IOC_WAKE | GPIO_IN)
#define FIONA_REV2_IOC_WAKE_POLARITY    		0

// USB Wake Sources
#define GPIO_FIONA_REV2_USB_WAKE                3
#define GPIO_FIONA_REV2_USB_WAKE_POLARITY       0

// WAN Wake Source (WAN's signal line to wake PXA on ring)
#define GPIO_FIONA_REV2_WAKE_ON_WAN             4
#define GPIO_FIONA_REV2_WAKE_ON_WAN_MD          (GPIO_FIONA_REV2_WAKE_ON_WAN | GPIO_IN) 
#define FIONA_REV2_WAKE_ON_WAN_ON_POLARITY      1
#define FIONA_REV2_WAKE_ON_WAN_INTR_IRQ         IRQ_GPIO(GPIO_FIONA_REV2_WAKE_ON_WAN) 

#define GPIO_FIONA_REV2_WAN_SWITCH              14
#define GPIO_FIONA_REV2_WAN_SWITCH_MD           (GPIO_FIONA_REV2_WAN_SWITCH | GPIO_IN) 
#define FIONA_REV2_WAN_SWITCH_ON_POLARITY       1
#define FIONA_REV2_WAN_SWITCH_INTR_IRQ          IRQ_GPIO(GPIO_FIONA_REV2_WAN_SWITCH) 

// IOC Reset Line for resetting IOC
#define GPIO_FIONA_REV2_IOC_RESET               48
#define GPIO_FIONA_REV2_IOC_RESET_MD            ( GPIO_FIONA_REV2_IOC_RESET | GPIO_OUT | GPIO_DFLT_HIGH )
#define FIONA_REV2_IOC_RESET_POLARITY           0

// EINK Temperature Sensor
#define GPIO_FIONA_TEMP_SENSOR                  51
#define GPIO_FIONA_TEMP_SENSOR_MD               ( GPIO_FIONA_TEMP_SENSOR | GPIO_OUT | GPIO_DFLT_LOW )
#define FIONA_REV2_TEMP_SENSOR_POLARITY         0

#define GPIO_FIONA_APOLLO_COMMON                80
#define GPIO_FIONA_APOLLO_COMMON_MD             ( GPIO_FIONA_APOLLO_COMMON | GPIO_OUT | GPIO_DFLT_LOW )

#define GPIO_FIONA_APOLLO_POWER                 82
#define GPIO_FIONA_APOLLO_POWER_MD              ( GPIO_FIONA_APOLLO_POWER | GPIO_OUT | GPIO_DFLT_HIGH )

// Headphone Detect Line
#define GPIO_FIONA_HPDET_REV2					72
#define GPIO_FIONA_HPDET_REV2_MD				( GPIO_FIONA_HPDET_REV2 | GPIO_IN )
#define FIONA_REV2_HPDET_IRQ					IRQ_GPIO(GPIO_FIONA_HPDET_REV2)

// Fiona Wake Sources
// On/Off Switch (0)
// IOC Wake (1)
// USB_DC   (3)
// WAN Wake (4)
// WAN Enable Switch (14)
#define CFG_FIONA_REV2_PWER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_REV2_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_REV2_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_REV2_USB_WAKE) |  \
                                                (1 << GPIO_FIONA_REV2_WAKE_ON_WAN) |  \
                                                (1 << GPIO_FIONA_REV2_WAN_SWITCH))

#define CFG_FIONA_REV2_PRER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_REV2_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_REV2_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_REV2_USB_WAKE))

#define CFG_FIONA_REV2_PFER                     (   0x0  |  \
                                                (1 << GPIO_FIONA_REV2_POWER_SWITCH) |  \
                                                (1 << GPIO_FIONA_REV2_IOC_WAKE) |  \
                                                (1 << GPIO_FIONA_REV2_USB_WAKE))

// GPIO settings during sleep (if output and set to 1 here, GPIO driven high during sleep)
#define CFG_FIONA_REV2_PGSR0					0xFEFFFEAF
#define CFG_FIONA_REV2_PGSR1					0x01EFFC7F
#define CFG_FIONA_REV2_PGSR2					0x0018FF90

#endif // __ASM_ARCH_FIONA_H__

