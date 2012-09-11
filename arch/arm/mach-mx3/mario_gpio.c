/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2007 Lab126, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/gpio.h>
#include "iomux.h"

#include <asm/arch/gpio.h>
#include <asm/arch/pmic_external.h>
#include <asm/arch/pmic_power.h>

#include <asm/arch/board_id.h>


// Local Prototypes
void gpio_sdhc_active(int module);

/*!
 * @file mach-mx3/mario_gpio.c
 *
 * @brief This file contains all the GPIO setup functions for the board.
 *
 * @ingroup GPIO_MX31
 */

void gpio_activate_audio_ports(void);

void mario_nor_write_enable(unsigned int enable)
{
    if (enable) {
        mxc_set_gpio_direction(MX31_PIN_GPIO1_0, 0);
        mxc_set_gpio_dataout(MX31_PIN_GPIO1_0,1);
    }
    else  {
        mxc_set_gpio_direction(MX31_PIN_GPIO1_0, 0);
        mxc_set_gpio_dataout(MX31_PIN_GPIO1_0,0);
    }
}

/*!
 * This system-wise GPIO function initializes the pins during system startup.
 * All the statically linked device drivers should put the proper GPIO initialization
 * code inside this function. It is called by \b fixup_mario() during
 * system startup. This function is board specific.
 */
void mario_gpio_init(void)
{
	mxc_request_iomux(MX31_PIN_ATA_CS1, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
	mxc_set_gpio_direction(MX31_PIN_ATA_CS1, 1);

	// carkit detect
	mxc_request_iomux(MX31_PIN_CTS1, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
	mxc_set_gpio_direction(MX31_PIN_CTS1, 1);

	/* Setup default mario gpios */
    // mario_iomux_init();

	/*Connect DAM ports 4 & 5 to enable audio I/O */
	//gpio_activate_audio_ports();

    // Make the NOR writable
	//mxc_request_gpio(MX31_PIN_GPIO1_0);
    //mario_nor_write_enable(1);

    // Enable moviNAND
    //gpio_sdhc_active(0);
}


/*!
 * Setup GPIO for a UART port to be active
 *
 * @param  port         a UART port
 * @param  no_irda      indicates if the port is used for SIR
 */
void gpio_uart_active(int port, int no_irda)
{
	/*
	 * Configure the IOMUX control registers for the UART signals
	 */
	switch (port) {
		/* UART 1 IOMUX Configs */
	case 0:
		mxc_request_iomux(MX31_PIN_RXD1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_TXD1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
		/* UART 2 IOMUX Configs */
	case 1:
        printk("UART(2) NOT SUPPORTED\n");
		break;
		/* UART 3 IOMUX Configs - expansion connector */
	case 2:
		mxc_request_iomux(MX31_PIN_CSPI3_MOSI, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI3_MISO, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI3_SCLK, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_CSPI3_SPI_RDY, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		break;
		/* UART 4 IOMUX Configs - IOC UART */
	case 3:
		mxc_request_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		break;
		/* UART 5 IOMUX Configs */
	case 4:
        printk("UART(5) NOT SUPPORTED\n");
		break;
	default:
		break;
	}
	/*
	 * TODO: Configure the Pad registers for the UART pins
	 */
}

/*!
 * Setup GPIO for a UART port to be inactive
 *
 * @param  port         a UART port
 * @param  no_irda      indicates if the port is used for SIR
 */
void gpio_uart_inactive(int port, int no_irda)
{
	switch (port) {
	case 0:
		mxc_request_gpio(MX31_PIN_RXD1);
		mxc_request_gpio(MX31_PIN_TXD1);

		mxc_free_iomux(MX31_PIN_RXD1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_TXD1, OUTPUTCONFIG_GPIO,
			       INPUTCONFIG_GPIO);
		break;
	case 1:
        printk("gpio_uart_inactive(2) NOT SUPPORTED \n");
		break;
	case 2:
		mxc_request_gpio(MX31_PIN_CSPI3_MOSI);
		mxc_request_gpio(MX31_PIN_CSPI3_MISO);
		mxc_request_gpio(MX31_PIN_CSPI3_SCLK);
		mxc_request_gpio(MX31_PIN_CSPI3_SPI_RDY);
		mxc_free_iomux(MX31_PIN_CSPI3_MOSI, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_CSPI3_MISO, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_CSPI3_SCLK, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_GPIO);
		mxc_free_iomux(MX31_PIN_CSPI3_SPI_RDY, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_GPIO);
		break;
	case 3:
		mxc_request_gpio(MX31_PIN_ATA_DIOR);
		mxc_request_gpio(MX31_PIN_ATA_DIOW);
		mxc_free_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_free_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		break;
	case 4:
        printk("gpio_uart_inactive(5) NOT SUPPORTED \n");
		break;
	default:
		break;
	}
}

/*!
 * Configure the IOMUX GPR register to receive shared SDMA UART events
 *
 * @param  port         a UART port
 */
void config_uartdma_event(int port)
{

	switch (port) {
	case 1:
		/* Configure to receive UART 2 SDMA events */
		mxc_iomux_set_gpr(MUX_PGP_FIRI, false);
		break;
	case 2:
		/* Configure to receive UART 3 SDMA events */
		mxc_iomux_set_gpr(MUX_CSPI1_UART3, true);
		break;
	case 4:
		/* Configure to receive UART 5 SDMA events */
		//mxc_iomux_set_gpr(MUX_CSPI3_UART5_SEL, true);
        printk("config_uartdma_event(4) - not supported\n");
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_uart_active);
EXPORT_SYMBOL(gpio_uart_inactive);
EXPORT_SYMBOL(config_uartdma_event);

/*!
 * Setup GPIO for Keypad  to be active
 *
 */
void gpio_keypad_active(void)
{
	/*
	 * Configure the IOMUX control register for keypad signals.
	 */
	mxc_request_iomux(MX31_PIN_KEY_COL0, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL1, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL2, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL3, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL4, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL5, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL6, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_COL7, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW0, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW1, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW2, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW3, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW4, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW5, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW6, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_KEY_ROW7, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
}

EXPORT_SYMBOL(gpio_keypad_active);

/*!
 * Setup GPIO for Keypad to be inactive
 *
 */
void gpio_keypad_inactive(void)
{
	mxc_free_iomux(MX31_PIN_KEY_COL0, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL1, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL2, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL3, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL4, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL5, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL6, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_COL7, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW0, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW1, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW2, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW3, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW4, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW5, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW6, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_free_iomux(MX31_PIN_KEY_ROW7, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
}

EXPORT_SYMBOL(gpio_keypad_inactive);


/*!
 * Setup GPIO for a CSPI device to be active
 *
 * @param  cspi_mod         an CSPI device
 */
void gpio_spi_active(int cspi_mod)
{
	switch (cspi_mod) {
	case 0:
		/* SPI1 */
		mxc_request_iomux(MX31_PIN_RI_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DCD_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DTR_DCE2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_RI_DCE1, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_DCD_DCE1, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_DTR_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DSR_DTE1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_DSR_DCE1, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);

	    mxc_iomux_set_gpr(MUX_PGP_CSPI_BB, true);

		break;
	case 1:
		/* SPI2 */
		mxc_request_iomux(MX31_PIN_CSPI2_MISO, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_MOSI, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_CSPI2_SS0, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
	case 2:
		/* SPI3 */
		/*
		   mxc_request_iomux(MX31_PIN_CSPI2_MISO, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_MOSI, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SCLK, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SPI_RDY, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		   mxc_request_iomux(MX31_PIN_CSPI2_SS2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
		 */
		break;
	default:
		break;
	}
}

/*!
 * Setup 1-Wire to be active
 */
void gpio_owire_active(void)
{
    /*
     * Configure the IOMUX control register for 1-wire signals.
     */
    iomux_config_mux(MX31_PIN_BATT_LINE, OUTPUTCONFIG_FUNC,
             INPUTCONFIG_FUNC);
    iomux_config_pad(MX31_PIN_BATT_LINE, PAD_CTL_LOOPBACK);
}

/*!
 * Setup 1-Wire to be active
 */
void gpio_owire_inactive(void)
{
    /*
     * Configure the IOMUX control register for 1-wire signals.
     */
    iomux_config_mux(MX31_PIN_BATT_LINE, OUTPUTCONFIG_GPIO,
             INPUTCONFIG_GPIO);
}

EXPORT_SYMBOL(gpio_owire_active);
EXPORT_SYMBOL(gpio_owire_inactive);

/*!
 * Setup GPIO for a CSPI device to be inactive
 *
 * @param  cspi_mod         a CSPI device
 */
void gpio_spi_inactive(int cspi_mod)
{
	/* Do nothing as CSPI pins doesn't have/support GPIO mode */
}

/*!
 * Setup GPIO for an I2C device to be active
 *
 * @param  i2c_num         an I2C device
 */
void gpio_i2c_active(int i2c_num)
{
	switch (i2c_num) {
	case 0:
		mxc_request_iomux(MX31_PIN_I2C_CLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_I2C_DAT, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		break;
	case 1:
	case 2:
        printk("cpio_i2c_active(%d) - NOT SUPPORTED\n",i2c_num);
		break;
	default:
		break;
	}

}

/*!
 * Setup GPIO for an I2C device to be inactive
 *
 * @param  i2c_num         an I2C device
 */
void gpio_i2c_inactive(int i2c_num)
{
	switch (i2c_num) {
	case 0:
		mxc_request_iomux(MX31_PIN_I2C_CLK, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_I2C_DAT, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_FUNC);
		break;
	case 1:
	case 2:
        printk("cpio_i2c_inactive(%d) - NOT SUPPORTED\n",i2c_num);
		break;
	default:
		break;
	}
}

/*!
 * This function configures the IOMux block for PMIC standard operations.
 *
 */
void gpio_pmic_active(void)
{
	mxc_request_iomux(MX31_PIN_GPIO1_3, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
	mxc_set_gpio_direction(MX31_PIN_GPIO1_3, 1);
//      mxc_set_gpio_edge_ctrl(MX31_PIN_GPIO1_3, GPIO_INT_RISE_EDGE);
}

EXPORT_SYMBOL(gpio_pmic_active);

/*!
 * This function activates DAM ports 4 & 5 to enable
 * audio I/O. Thsi function is called from mario_gpio_init
 * function, which is board-specific.
 */
void gpio_activate_audio_ports(void)
{
	/* config Audio ports (4 & 5) */
	mxc_request_iomux(MX31_PIN_SCK4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SRXD4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_STXD4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SFS4, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SCK5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SRXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_STXD5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_SFS5, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
}

/*!
 * Setup GPIO for SDHC to be active
 *
 * @param module SDHC module number
 */
void gpio_sdhc_active(int module)
{
	switch (module) {
	case 0:     // moviNAND on Mario
		mxc_request_iomux(MX31_PIN_SD1_CLK, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_CMD, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA0, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA1, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA2, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);
		mxc_request_iomux(MX31_PIN_SD1_DATA3, OUTPUTCONFIG_FUNC,
				  INPUTCONFIG_FUNC);

		mxc_iomux_set_pad(MX31_PIN_SD1_CLK,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		mxc_iomux_set_pad(MX31_PIN_SD1_CMD,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA0,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA1,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA2,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA3,
				  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_47K_PU));
		break;

	case 1:     // SD/MMC Card Slot on Mario
		mxc_request_iomux(MX31_PIN_PC_CD2_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_CD1_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_WAIT_B, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_READY, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_VS1, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);
		mxc_request_iomux(MX31_PIN_PC_PWRON, OUTPUTCONFIG_ALT1,
				  INPUTCONFIG_ALT1);

#if 0
        mxc_iomux_set_pad(MX31_PIN_PC_CD2_B,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
        mxc_iomux_set_pad(MX31_PIN_PC_CD1_B,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
        mxc_iomux_set_pad(MX31_PIN_PC_WAIT_B,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
        mxc_iomux_set_pad(MX31_PIN_PC_READY,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
        mxc_iomux_set_pad(MX31_PIN_PC_VS1,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
        mxc_iomux_set_pad(MX31_PIN_PC_PWRON,
                  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
#endif
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_sdhc_active);

/*!
 * Setup GPIO for SDHC1 to be inactive
 *
 * @param module SDHC module number
 */
void gpio_sdhc_inactive(int module)
{
	switch (module) {
	case 0:
		mxc_request_iomux(MX31_PIN_SD1_CLK, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_CMD, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA0, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA1, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA2, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_SD1_DATA3, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);

		mxc_iomux_set_pad(MX31_PIN_SD1_CLK,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		mxc_iomux_set_pad(MX31_PIN_SD1_CMD,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA0,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA1,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA2,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		mxc_iomux_set_pad(MX31_PIN_SD1_DATA3,
				  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW | PAD_CTL_100K_PD));
		break;

	case 1:
		/* TODO:what are the pins for SDHC2? */
		mxc_request_iomux(MX31_PIN_PC_CD2_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_CD1_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_WAIT_B, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_READY, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_VS1, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);
		mxc_request_iomux(MX31_PIN_PC_PWRON, OUTPUTCONFIG_GPIO,
				  INPUTCONFIG_NONE);

#if 0
        mxc_iomux_set_pad(MX31_PIN_PC_CD2_B,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
        mxc_iomux_set_pad(MX31_PIN_PC_CD1_B,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
        mxc_iomux_set_pad(MX31_PIN_PC_WAIT_B,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
        mxc_iomux_set_pad(MX31_PIN_PC_READY,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
        mxc_iomux_set_pad(MX31_PIN_PC_VS1,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
        mxc_iomux_set_pad(MX31_PIN_PC_PWRON,
                  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
#endif
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(gpio_sdhc_inactive);

/*
 * Probe for the card. If present the GPIO data would be set.
 */
int sdhc_get_card_det_status(struct device *dev)
{
	if (to_platform_device(dev)->id == 0) {
        // moviNAND - FIXME - must munge dev structure to fill out GPIO data
        // according to routine comment....
		return(0);
	} else {
		return mxc_get_gpio_datain(MX31_PIN_NFCLE);
	}
}

EXPORT_SYMBOL(sdhc_get_card_det_status);

/*
 * Return the card detect pin.
 */
int sdhc_init_card_det(int id)
{
	if (id == 1) {
        printk("sdhc_init_card_det() for mmcCard\n");
		iomux_config_mux(MX31_PIN_NFCLE, OUTPUTCONFIG_GPIO,
				 INPUTCONFIG_GPIO);
		return IOMUX_TO_IRQ(MX31_PIN_NFCLE);
	} else {
        printk("sdhc_init_card_det(%d) for moviNAND.\n",id);
        return(IOMUX_TO_IRQ(MX31_PIN_CSI_D4));
	}
}

EXPORT_SYMBOL(sdhc_init_card_det);

/*!
 * Setup GPIO for LCD to be active
 *
 */
void gpio_lcd_active(void)
{
    mxc_request_iomux(MX31_PIN_LD0,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD1,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD2,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD3,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD4,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD5,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD6,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD7,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD8,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD9,     OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD10,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD11,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD12,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD13,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD14,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD15,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD16,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    mxc_request_iomux(MX31_PIN_LD17,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
    
    mxc_request_iomux(MX31_PIN_VSYNC3,  OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // VSYNC
    mxc_request_iomux(MX31_PIN_HSYNC,   OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // HSYNC
    mxc_request_iomux(MX31_PIN_FPSHIFT, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // CLK
    mxc_request_iomux(MX31_PIN_DRDY0,   OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // DRDY
    mxc_request_iomux(MX31_PIN_D3_REV,  OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // REV
    mxc_request_iomux(MX31_PIN_CONTRAST,OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // CONTR
    mxc_request_iomux(MX31_PIN_D3_SPL,  OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // SPL
    mxc_request_iomux(MX31_PIN_D3_CLS,  OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // CLS
}

EXPORT_SYMBOL(gpio_lcd_active);

/*!
 * Setup GPIO for LCD to be inactive
 *
 */
void gpio_lcd_inactive(void)
{
}

EXPORT_SYMBOL(gpio_lcd_inactive);

/*!
 * Setup pins for SLCD to be active
 *
 */
void slcd_gpio_config(void)
{
    mxc_request_iomux(MX31_PIN_READ,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // READ
    mxc_request_iomux(MX31_PIN_WRITE,   OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // WRITE
    mxc_request_iomux(MX31_PIN_PAR_RS,  OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // RS
    mxc_request_iomux(MX31_PIN_LCS0,    OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);   // CHIP SELECT
}

EXPORT_SYMBOL(slcd_gpio_config);

/*!
 * Setup pins for Broadsheet/ISIS.
 */
#define BROADSHEET_HIRQ_LINE		MX31_PIN_SD_D_CLK
#define BROADSHEET_HRST_LINE		MX31_PIN_SD_D_IO
#define BROADSHEET_HRDY_LINE		MX31_PIN_SD_D_I
#define BROADSHEET_RESET_VAL		0
#define BROADSHEET_NON_RESET_VAL	1
#define WR_GPIO_LINE(addr, val)		mxc_set_gpio_dataout(addr, val);
#define BROADSHEET_HIRQ_IRQ		IOMUX_TO_IRQ(BROADSHEET_HIRQ_LINE)

#define BROADSHEET_GPIO_INIT_SUCCESS	0
#define BROADSHEET_HIRQ_RQST_FAILURE	1
#define BROADSHEET_HIRQ_INIT_FAILURE	2
#define BROADSHEET_HRST_INIT_FAILURE	3
#define BROADSHEET_HRDY_INIT_FAILURE	4

int broadsheet_gpio_config(irq_handler_t broadsheet_irq_handler, char *broadsheet_irq_handler_name)
{
	int result = BROADSHEET_GPIO_INIT_SUCCESS;
	
	gpio_lcd_active();
	slcd_gpio_config();
	
	if ( broadsheet_irq_handler )
	{
		// Set up IRQ for for Broadsheet HIRQ line.
		//
		disable_irq(BROADSHEET_HIRQ_IRQ);
		set_irq_type(BROADSHEET_HIRQ_IRQ, IRQF_TRIGGER_RISING);
		
		if ( request_irq(BROADSHEET_HIRQ_IRQ, broadsheet_irq_handler, 0, broadsheet_irq_handler_name, NULL) )
			result = BROADSHEET_HIRQ_RQST_FAILURE;
		else
		{
			if ( mxc_request_gpio(BROADSHEET_HIRQ_LINE) )
				result = BROADSHEET_HIRQ_INIT_FAILURE;
			else
			{
				// Set HIRQ pin as input.
				//
				mxc_set_gpio_direction(BROADSHEET_HIRQ_LINE, 1);
			}
		}
	}
	
	if ( BROADSHEET_GPIO_INIT_SUCCESS == result )
	{
		if ( mxc_request_gpio(BROADSHEET_HRST_LINE) )
			result = BROADSHEET_HRST_INIT_FAILURE;
		else
		{
			// Set HRST pin as an output and initialize it to zero (it's active LOW).
			//
			mxc_set_gpio_direction(BROADSHEET_HRST_LINE, 0);
			mxc_set_gpio_dataout(BROADSHEET_HRST_LINE, 0);
		}
	}
	
	if ( BROADSHEET_GPIO_INIT_SUCCESS == result )
	{
		if ( mxc_request_gpio(BROADSHEET_HRDY_LINE) )
			result = BROADSHEET_HRDY_INIT_FAILURE;
		else
		{
			// Set HRDY pin as an input.
			//
			mxc_set_gpio_direction(BROADSHEET_HRDY_LINE, 1);
		}
	}
	
	return ( result );
}

void broadsheet_gpio_disable(int disable_bs_irq)
{
	if ( disable_bs_irq )
	{
		disable_irq(BROADSHEET_HIRQ_IRQ);
		free_irq(BROADSHEET_HIRQ_IRQ, NULL);
		
		mxc_free_gpio(BROADSHEET_HIRQ_LINE);
	}
	
	mxc_free_gpio(BROADSHEET_HRST_LINE);
	mxc_free_gpio(BROADSHEET_HRDY_LINE);
}

void broadsheet_reset(void)
{
	WR_GPIO_LINE(BROADSHEET_HRST_LINE, BROADSHEET_RESET_VAL);	// Assert RST.
	mdelay(100);	// Pause 100 ms during reset.
	WR_GPIO_LINE(BROADSHEET_HRST_LINE, BROADSHEET_NON_RESET_VAL);	// Clear RST.
	mdelay(400);	// Pause 400 ms to allow Broasheet time to come up.
}

int broadsheet_ready(void)
{
	return ( mxc_get_gpio_datain(BROADSHEET_HRDY_LINE) );
}

EXPORT_SYMBOL(broadsheet_gpio_config);
EXPORT_SYMBOL(broadsheet_gpio_disable);
EXPORT_SYMBOL(broadsheet_ready);
EXPORT_SYMBOL(broadsheet_reset);

/*!
 * Setup GPIO for ATA interface
 *
 */
void gpio_ata_active(void)
{
    printk("gpio_ata_active() called - FIXME - disable this !!\n");
    // FIXME - port this routine
    return;

	/*
	 * Configure the GPR for ATA group B signals
	 */

	mxc_iomux_set_gpr(MUX_PGP_ATA_7 | MUX_PGP_ATA_6 | MUX_PGP_ATA_2 |
			  MUX_PGP_ATA_1, true);

	/*
	 * Configure the IOMUX for ATA group B signals
	 */

	mxc_request_iomux(MX31_PIN_CSPI1_MOSI, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D0
	mxc_request_iomux(MX31_PIN_CSPI1_MISO, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D1
	mxc_request_iomux(MX31_PIN_CSPI1_SS0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D2
	mxc_request_iomux(MX31_PIN_CSPI1_SS1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D3
	mxc_request_iomux(MX31_PIN_CSPI1_SS2, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D4
	mxc_request_iomux(MX31_PIN_CSPI1_SCLK, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D5
	mxc_request_iomux(MX31_PIN_CSPI1_SPI_RDY, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D6
	mxc_request_iomux(MX31_PIN_STXD3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D7
	mxc_request_iomux(MX31_PIN_SRXD3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D8
	mxc_request_iomux(MX31_PIN_SCK3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D9
	mxc_request_iomux(MX31_PIN_SFS3, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D10
	mxc_request_iomux(MX31_PIN_STXD6, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D11
	mxc_request_iomux(MX31_PIN_SRXD6, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D12
	mxc_request_iomux(MX31_PIN_SCK6, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D13
	mxc_request_iomux(MX31_PIN_CAPTURE, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D14
	mxc_request_iomux(MX31_PIN_COMPARE, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_D15

	mxc_request_iomux(MX31_PIN_USBH2_STP, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_DMARQ_B
	mxc_request_iomux(MX31_PIN_USBH2_CLK, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_INTRQ_B
	mxc_request_iomux(MX31_PIN_USBH2_NXT, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_DA0
	mxc_request_iomux(MX31_PIN_USBH2_DATA0, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_DA1
	mxc_request_iomux(MX31_PIN_USBH2_DATA1, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_DA2
	mxc_request_iomux(MX31_PIN_USBH2_DIR, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);	// ATA_BUFFER_DIR

	/* These ATA pins are common to Group A and Group B */

	mxc_request_iomux(MX31_PIN_ATA_CS0, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_ATA_CS1, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_ATA_DMACK, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_ATA_RESET_B, OUTPUTCONFIG_FUNC,
			  INPUTCONFIG_FUNC);
	mxc_request_iomux(MX31_PIN_PWMO, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);

	/* Need fast slew rate for UDMA mode */

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 0
	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 1
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 2
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 3
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 4
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 5
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 6
	mxc_iomux_set_pad(MX31_PIN_STXD3, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 7
	mxc_iomux_set_pad(MX31_PIN_SRXD3, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 8
	mxc_iomux_set_pad(MX31_PIN_SCK3, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 9
	mxc_iomux_set_pad(MX31_PIN_SFS3, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 10
	mxc_iomux_set_pad(MX31_PIN_STXD6, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 11
	mxc_iomux_set_pad(MX31_PIN_SRXD6, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 12
	mxc_iomux_set_pad(MX31_PIN_SCK6, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 13
	mxc_iomux_set_pad(MX31_PIN_CAPTURE, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 14
	mxc_iomux_set_pad(MX31_PIN_COMPARE, PAD_CTL_SRE_FAST | PAD_CTL_PKE_NONE);	// data 12

	/*
	 * Turn off default pullups on high asserted control signals.
	 * These are pulled down externally, so it will just waste
	 * power and create voltage divider action to pull them up
	 * on chip.
	 */
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, PAD_CTL_PKE_NONE);	// ATA_DMARQ
	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, PAD_CTL_PKE_NONE);	// ATA_INTRQ
}

EXPORT_SYMBOL(gpio_ata_active);

/*!
 * Restore ATA interface pins to reset values
 *
 */
void gpio_ata_inactive(void)
{
    // FIXME - port this routine
    return;

	/*
	 * Turn off ATA group B signals
	 */
	mxc_request_iomux(MX31_PIN_CSPI1_MOSI, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D0
	mxc_request_iomux(MX31_PIN_CSPI1_MISO, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D1
	mxc_request_iomux(MX31_PIN_CSPI1_SS0, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D2
	mxc_request_iomux(MX31_PIN_CSPI1_SS1, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D3
	mxc_request_iomux(MX31_PIN_CSPI1_SS2, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D4
	mxc_request_iomux(MX31_PIN_CSPI1_SCLK, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D5
	mxc_request_iomux(MX31_PIN_CSPI1_SPI_RDY, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D6
	mxc_request_iomux(MX31_PIN_STXD3, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D7
	mxc_request_iomux(MX31_PIN_SRXD3, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D8
	mxc_request_iomux(MX31_PIN_SCK3, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D9
	mxc_request_iomux(MX31_PIN_SFS3, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D10
	mxc_request_iomux(MX31_PIN_STXD6, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D11
	mxc_request_iomux(MX31_PIN_SRXD6, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D12
	mxc_request_iomux(MX31_PIN_SCK6, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D13
	mxc_request_iomux(MX31_PIN_CAPTURE, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D14
	mxc_request_iomux(MX31_PIN_COMPARE, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_D15

	mxc_request_iomux(MX31_PIN_USBH2_STP, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_DMARQ_B
	mxc_request_iomux(MX31_PIN_USBH2_CLK, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_INTRQ_B
	mxc_request_iomux(MX31_PIN_USBH2_NXT, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_DA0
	mxc_request_iomux(MX31_PIN_USBH2_DATA0, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_DA1
	mxc_request_iomux(MX31_PIN_USBH2_DATA1, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_DA2
	mxc_request_iomux(MX31_PIN_USBH2_DIR, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);	// ATA_BUFFER_DIR

	/* These ATA pins are common to Group A and Group B */

	mxc_request_iomux(MX31_PIN_ATA_CS0, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_ATA_CS1, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_ATA_DIOR, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_ATA_DIOW, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_ATA_DMACK, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);
	mxc_request_iomux(MX31_PIN_ATA_RESET_B, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_NONE);

	/* Needed fast slew rate for UDMA mode */

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 0
	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 1
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 2
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 3
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 4
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 5
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 6
	mxc_iomux_set_pad(MX31_PIN_STXD3, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 7
	mxc_iomux_set_pad(MX31_PIN_SRXD3, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 8
	mxc_iomux_set_pad(MX31_PIN_SCK3, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 9
	mxc_iomux_set_pad(MX31_PIN_SFS3, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 10
	mxc_iomux_set_pad(MX31_PIN_STXD3, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 11
	mxc_iomux_set_pad(MX31_PIN_SRXD6, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 12
	mxc_iomux_set_pad(MX31_PIN_SCK6, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 13
	mxc_iomux_set_pad(MX31_PIN_CAPTURE, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 14
	mxc_iomux_set_pad(MX31_PIN_COMPARE, PAD_CTL_SRE_SLOW | PAD_CTL_PKE_NONE);	// data 12
}

EXPORT_SYMBOL(gpio_ata_inactive);

/*!
 * Setup EDIO/IOMUX for external UART.
 *
 * @param port UART port
 * @param irq Interrupt line to allocate
 * @param handler Function to be called when the IRQ occurs
 * @param irq_flags Interrupt type flags
 * @param devname An ascii name for the claiming device
 * @param dev_id A cookie passed back to the handler function
 * @return  Returns 0 if the interrupt was successfully requested,
 *          otherwise returns an error code.
 */
int extuart_intr_setup(unsigned int port, unsigned int irq,
		       irqreturn_t(*handler) (int, void *),
		       unsigned long irq_flags, const char *devname,
		       void *dev_id)
{
	return 0;
}

/*!
 * Get the EDIO interrupt, clear if set.
 *
 * @param port UART port
 */
void extuart_intr_clear(unsigned int port)
{
}

/*!
 * Do IOMUX configs required to put the
 * pin back in low power mode.
 *
 * @param port UART port
 * @param irq Interrupt line to free
 * @param dev_id Device identity to free
 * @return  Returns 0 if the interrupt was successfully freed,
 *          otherwise returns an error code.
 */
int extuart_intr_cleanup(unsigned int port, unsigned int irq, void *dev_id)
{
	return 0;
}

/* *INDENT-OFF* */
/*
 * USB Host 1
 * pins conflict with SPI1, ATA, UART3
 */
int gpio_usbh1_active(void)
{
	if (mxc_request_iomux(MX31_PIN_CSPI1_MOSI,	/* USBH1_RXDM */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_MISO,	/* USBH1_RXDP */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS0,	/* USBH1_TXDM */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS1,	/* USBH1_TXDP */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SS2,	/* USBH1_RCV  */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SCLK,	/* USBH1_OEB (_TXOE) */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) ||
	    mxc_request_iomux(MX31_PIN_CSPI1_SPI_RDY,	/* USBH1_FS   */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1) || 
	    mxc_request_iomux(MX31_PIN_GPIO1_4,	        /* USBH1_SUSPEND  */
			      OUTPUTCONFIG_ALT1, INPUTCONFIG_ALT1)) {
		return -EINVAL;
	}

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI,		/* USBH1_RXDM */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO,		/* USBH1_RXDP */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0,		/* USBH1_TXDM */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1,		/* USBH1_TXDP */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2,		/* USBH1_RCV  */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK,		/* USBH1_OEB (_TXOE) */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY,	/* USBH1_FS   */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_iomux_set_pad(MX31_PIN_GPIO1_4,	        /* USBH1_SUSPEND   */
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	return 0;
}

EXPORT_SYMBOL(gpio_usbh1_active);

void gpio_usbh1_inactive(void)
{
	mxc_free_iomux(MX31_PIN_CSPI1_MOSI,		/* USBH1_RXDM */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_MISO,		/* USBH1_RXDP */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_SS0,		/* USBH1_TXDM */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_SS1,		/* USBH1_TXDP */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_SS2,		/* USBH1_RCV  */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_SCLK,		/* USBH1_OEB (_TXOE) */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_CSPI1_SPI_RDY,		/* USBH1_FS   */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_GPIO1_4,		/* USBH1_SUSPEND   */
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);

	mxc_iomux_set_pad(MX31_PIN_CSPI1_MOSI,		/* USBH1_RXDM */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_MISO,		/* USBH1_RXDP */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS0,		/* USBH1_TXDM */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS1,		/* USBH1_TXDP */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SS2,		/* USBH1_RCV  */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SCLK,		/* USBH1_OEB (_TXOE) */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_CSPI1_SPI_RDY,	/* USBH1_FS   */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_GPIO1_4,         /* USBH1_SUSPEND   */
		       (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));

	mxc_request_iomux(MX31_PIN_GPIO1_4, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_set_gpio_direction(MX31_PIN_GPIO1_4, 0);
	mxc_set_gpio_dataout(MX31_PIN_GPIO1_4, 1);
}

EXPORT_SYMBOL(gpio_usbh1_inactive);

/*
 * USB Host 2
 * pins conflict with UART5, PCMCIA
 */
int gpio_usbh2_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBH2_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBH2_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) //||

		)
	{
		return -EINVAL;
	}

//#define H2_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST | PAD_CTL_HYS_CMOS | PAD_CTL_ODE_CMOS | PAD_CTL_100K_PU)
#define H2_PAD_CFG (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST )
	mxc_iomux_set_pad(MX31_PIN_USBH2_CLK, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DIR, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_NXT, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_STP, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA0, H2_PAD_CFG);
	mxc_iomux_set_pad(MX31_PIN_USBH2_DATA1, H2_PAD_CFG);

	mxc_iomux_set_pad(MX31_PIN_STXD3, H2_PAD_CFG);	/* USBH2_DATA2 */
	mxc_iomux_set_pad(MX31_PIN_SRXD3, H2_PAD_CFG);	/* USBH2_DATA3 */
	mxc_iomux_set_pad(MX31_PIN_SCK3, H2_PAD_CFG);	/* USBH2_DATA4 */
	mxc_iomux_set_pad(MX31_PIN_SFS3, H2_PAD_CFG);	/* USBH2_DATA5 */
	mxc_iomux_set_pad(MX31_PIN_STXD6, H2_PAD_CFG);	/* USBH2_DATA6 */
	mxc_iomux_set_pad(MX31_PIN_SRXD6, H2_PAD_CFG);	/* USBH2_DATA7 */


#undef H2_PAD_CFG

	mxc_iomux_set_gpr(MUX_PGP_UH2, true);

#if 0 
	do { //turn on 1.8V power
		t_regulator_voltage volt;
		volt.sw2a = SW2A_1_8V;
		pmic_power_regulator_set_voltage(SW_SW2A, volt);
	}while (0);
#endif

	/* CS is active low. Turn it on */ 
	mxc_request_iomux(MX31_PIN_NFRE_B, 
			  OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
        mxc_set_gpio_direction(MX31_PIN_NFRE_B, 0);
        mxc_set_gpio_dataout(MX31_PIN_NFRE_B,0);

	return 0;
}

EXPORT_SYMBOL(gpio_usbh2_active);

void gpio_usbh2_inactive(void)
{
        mxc_set_gpio_dataout(MX31_PIN_NFRE_B,1);

	iomux_config_gpr(MUX_PGP_UH2, false);

	iomux_config_pad(MX31_PIN_USBH2_CLK,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DIR,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_NXT,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_STP,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DATA0,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_USBH2_DATA1,
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SRXD6,		/* USBH2_DATA2 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_STXD6,		/* USBH2_DATA3 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SFS3,			/* USBH2_DATA4 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SCK3,			/* USBH2_DATA5 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_SRXD3,		/* USBH2_DATA6 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));
	iomux_config_pad(MX31_PIN_STXD3,		/* USBH2_DATA7 */
			 (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_FAST));

	mxc_free_iomux(MX31_PIN_USBH2_CLK,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBH2_DIR,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBH2_NXT,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBH2_STP,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBH2_DATA0,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBH2_DATA1,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_STXD3,		/* USBH2_DATA2 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_SRXD3,		/* USBH2_DATA3 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_SCK3,		/* USBH2_DATA4 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_SFS3,		/* USBH2_DATA5 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_STXD6,		/* USBH2_DATA6 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_SRXD6,		/* USBH2_DATA7 */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_NFRE_B,		/* USB HS Enable */
			      OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
}

EXPORT_SYMBOL(gpio_usbh2_inactive);

/*
 * USB OTG HS port
 */
int gpio_usbotg_hs_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBOTG_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA2,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA3,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA4,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA5,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA6,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA7,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC)) {
		return -EINVAL;
	}


	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP,
			  (PAD_CTL_DRV_MAX | PAD_CTL_SRE_FAST));

	mxc_request_iomux(MX31_PIN_GPIO1_6, 
			  OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
        mxc_set_gpio_direction(MX31_PIN_GPIO1_6, 0);
        mxc_set_gpio_dataout(MX31_PIN_GPIO1_6,0);

	mxc_request_iomux(MX31_PIN_GPIO3_0, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
	mxc_set_gpio_direction(MX31_PIN_GPIO3_0, 0);
	mxc_set_gpio_dataout(MX31_PIN_GPIO3_0, 1);

	return 0;

	
}

EXPORT_SYMBOL(gpio_usbotg_hs_active);

void gpio_usbotg_hs_inactive(void)
{
        mxc_set_gpio_dataout(MX31_PIN_GPIO1_6,1);
	mxc_set_gpio_dataout(MX31_PIN_GPIO3_0, 0);

	mxc_free_iomux(MX31_PIN_USBOTG_DATA0,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA1,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA2,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA3,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA4,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA5,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA6,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA7,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_CLK,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DIR,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_NXT,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_STP,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_GPIO1_6, 
			  OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);
	mxc_free_iomux(MX31_PIN_GPIO3_0, OUTPUTCONFIG_GPIO,
			  INPUTCONFIG_GPIO);

	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA0,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA1,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA2,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA3,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA4,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA5,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA6,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DATA7,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_CLK,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_DIR,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_NXT,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
	mxc_iomux_set_pad(MX31_PIN_USBOTG_STP,
			  (PAD_CTL_DRV_NORMAL | PAD_CTL_SRE_SLOW));
}

EXPORT_SYMBOL(gpio_usbotg_hs_inactive);

/*
 * USB OTG FS port
 */
int gpio_usbotg_fs_active(void)
{
	if (mxc_request_iomux(MX31_PIN_USBOTG_DATA0,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA1,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA2,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA3,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA4,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA5,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA6,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DATA7,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_CLK,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_DIR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_NXT,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USBOTG_STP,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC) ||
	    mxc_request_iomux(MX31_PIN_USB_PWR,
			      OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC)) {
		return -EINVAL;
	}
	return 0;

}

EXPORT_SYMBOL(gpio_usbotg_fs_active);

void gpio_usbotg_fs_inactive(void)
{
	mxc_free_iomux(MX31_PIN_USBOTG_DATA0,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA1,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA2,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA3,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA4,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA5,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA6,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DATA7,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_CLK,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_DIR,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_NXT,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USBOTG_STP,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_free_iomux(MX31_PIN_USB_PWR,
		       OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
}

EXPORT_SYMBOL(gpio_usbotg_fs_inactive);
/* *INDENT-ON* */

/*!
 * Setup GPIO for PCMCIA interface
 *
 */
void gpio_pcmcia_active(void)
{
}

EXPORT_SYMBOL(gpio_pcmcia_active);

/*!
 * Setup GPIO for pcmcia to be inactive
 */
void gpio_pcmcia_inactive(void)
{
}

EXPORT_SYMBOL(gpio_pcmcia_inactive);
/*!
 * Setup IR to be used by UART and FIRI
 */
void gpio_firi_init(void)
{
}

EXPORT_SYMBOL(gpio_firi_init);

/*!
 * Setup IR to be used by UART
 */
void gpio_firi_inactive(void)
{
    printk("gpio_firi_inactive() not supported\n");
}

EXPORT_SYMBOL(gpio_firi_inactive);

/*!
 * Setup IR to be used by FIRI
 */
void gpio_firi_active(void *fir_cong_reg_base, unsigned int tpp_mask)
{
    printk("gpio_firi_active() not supported\n");
}

EXPORT_SYMBOL(gpio_firi_active);


// - - - - WAN interface - - - -

static int wan_use_flip_flop = 0;

void gpio_wan_power(int enable)
{
	if (enable) {
		pmic_power_regulator_on(REGU_GPO3);
	} else {
		pmic_power_regulator_off(REGU_GPO3);
	}

	if (wan_use_flip_flop) {
		mdelay(1);
		pmic_power_regulator_on(REGU_GPO2);
		mdelay(1);
		pmic_power_regulator_off(REGU_GPO2);
		mdelay(1);
	}
}

EXPORT_SYMBOL(gpio_wan_power);

void gpio_wan_cfg_rf_enable(int opendrain)
{
	mxc_iomux_set_pad(MX31_PIN_GPIO1_1, PAD_CTL_DRV_NORMAL | (opendrain ? PAD_CTL_ODE_OpenDrain : 0));
}

EXPORT_SYMBOL(gpio_wan_cfg_rf_enable);

void gpio_wan_rf_enable(int enable)
{
	mxc_set_gpio_dataout(MX31_PIN_GPIO1_1, enable != 0);
}

EXPORT_SYMBOL(gpio_wan_rf_enable);

void gpio_wan_usb_enable(int enable)
{
	if (!IS_MARIO()) {
		mxc_set_gpio_dataout(MX31_PIN_DCD_DCE1, enable != 0);
	}
}

EXPORT_SYMBOL(gpio_wan_usb_enable);


#define IS_TURING_EVT1() (IS_TURING() && IS_EVT() && GET_BOARD_HW_VERSION() == 1)

void gpio_wan_init(void *tph_event_callback)
{
	// configure the "WAN_RF_ENABLE" line
	mxc_request_iomux(MX31_PIN_GPIO1_1, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
	mxc_set_gpio_direction(MX31_PIN_GPIO1_1, 0);

	// default to push/pull until reconfigured
	gpio_wan_cfg_rf_enable(0);

	// bring "WAN_RF_ENABLE" low by default
	gpio_wan_rf_enable(0);

	if (!IS_MARIO()) {
		// configure the "WAN_USB_EN" line
		mxc_request_iomux(MX31_PIN_DCD_DCE1, OUTPUTCONFIG_GPIO, INPUTCONFIG_NONE);
		mxc_set_gpio_direction(MX31_PIN_DCD_DCE1, 0);
		mxc_iomux_set_pad(MX31_PIN_DCD_DCE1, PAD_CTL_DRV_NORMAL);

		// bring "WAN_USB_EN" low by default
		gpio_wan_usb_enable(0);
	}

	wan_use_flip_flop = 1;

	if (IS_TURING()) {
		if (IS_EVT() && GET_BOARD_HW_VERSION() <= 1) {
			wan_use_flip_flop = 0;
		}

	} else if (IS_NELL() && IS_PROTO()) {
		wan_use_flip_flop = 0;
	}

	if (wan_use_flip_flop) {
		// ensure that the clock line is low
		pmic_power_regulator_off(REGU_GPO2);

		// ensure the power state is cleared
		gpio_wan_power(0);

	} else if (IS_TURING_EVT1()) {
		// leave USBHC1_PWR high (VBUS line for the J5 connector)
		pmic_power_regulator_on(REGU_GPO2);
	}

	// enable switcher for WAN (set to LOW_POWER on board initialization)
	pmic_power_switcher_set_mode(SW_SW2A, PULSE_SKIP, 0);

	// set up the PMIC event configuration for the "ON2B" line
	pmic_power_set_conf_button(BT_ON2B, 0, 0);
	pmic_power_event_sub(PWR_IT_ONOFD2I, tph_event_callback);
}

EXPORT_SYMBOL(gpio_wan_init);


void gpio_wan_exit(void *tph_event_callback)
{
	// clear the PMIC event handler
	pmic_power_event_unsub(PWR_IT_ONOFD2I, tph_event_callback);

	if (!wan_use_flip_flop && IS_TURING_EVT1()) {
		// power off USBHC1_PWR
		pmic_power_regulator_off(REGU_GPO2);
	}
}

EXPORT_SYMBOL(gpio_wan_exit);


// The following routine simply configures a GPIO pin with internal pullup
// resistor enabled.
// Use with caution; you need to understand the hardware!

void mario_enable_pullup_pad(iomux_pin_name_t pin)
{
	// Set up the desired pin with pullup resistor enabled
	mxc_iomux_set_pad(pin, (PAD_CTL_PKE_ENABLE | PAD_CTL_PUE_PUD));
}

EXPORT_SYMBOL(mario_enable_pullup_pad);


// The following routine simply turns off the internal pullup of a GPIO pin
// Use with caution; you need to understand the hardware!

void mario_disable_pullup_pad(iomux_pin_name_t pin)
{
	// Set up the desired pin with pullup resistor disabled
	mxc_iomux_set_pad(pin, PAD_CTL_PKE_ENABLE);
}

EXPORT_SYMBOL(mario_disable_pullup_pad);


