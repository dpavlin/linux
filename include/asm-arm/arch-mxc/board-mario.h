/*
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __ASM_ARCH_MXC_BOARD_MARIO_MX_H__
#define __ASM_ARCH_MXC_BOARD_MARIO_MX_H__

/*!
 * @defgroup BRDCFG_MX31 Board Configuration Options
 * @ingroup MSL_MX31
 */

/*!
 * @file arch-mxc/board-mx31ads.h
 *
 * @brief This file contains all the board level configuration options.
 *
 * It currently hold the options defined for MX31 ADS Platform.
 *
 * @ingroup BRDCFG_MX31
 */

/*
 * Include Files
 */
//#include <asm/arch/board.h>

/* Start of physical RAM */
#define PHYS_OFFSET	        UL(0x80000000)

/* Size of contiguous memory for DMA and other h/w blocks */
#define CONSISTENT_DMA_SIZE	SZ_8M

/*!
 * @name MXC UART EVB board level configurations
 */
/*! @{ */
/*!
 * Specify the max baudrate for the MXC UARTs for your board, do not specify a max
 * baudrate greater than 1500000. This is used while specifying the UART Power
 * management constraints.
 */
#define MAX_UART_BAUDRATE       1500000
/*!
 * Specifies if the Irda transmit path is inverting
 */
#define MXC_IRDA_TX_INV         0
/*!
 * Specifies if the Irda receive path is inverting
 */
#define MXC_IRDA_RX_INV         0
/* UART 1 configuration */
/*!
 * This define specifies if the UART port is configured to be in DTE or
 * DCE mode. There exists a define like this for each UART port. Valid
 * values that can be used are \b MODE_DTE or \b MODE_DCE.
 */
#define UART1_MODE              MODE_DCE
/*!
 * This define specifies if the UART is to be used for IRDA. There exists a
 * define like this for each UART port. Valid values that can be used are
 * \b IRDA or \b NO_IRDA.
 */
#define UART1_IR                NO_IRDA
/*!
 * This define is used to enable or disable a particular UART port. If
 * disabled, the UART will not be registered in the file system and the user
 * will not be able to access it. There exists a define like this for each UART
 * port. Specify a value of 1 to enable the UART and 0 to disable it.
 */
#define UART1_ENABLED           1
/*! @} */
/* UART 2 configuration */
#define UART2_MODE              MODE_DCE
#define UART2_IR                IRDA
#ifdef CONFIG_MXC_FIR_MODULE
#define UART2_ENABLED           0
#else
#define UART2_ENABLED           1
#endif
/* UART 3 configuration */
#define UART3_MODE              MODE_DTE
#define UART3_IR                NO_IRDA
#define UART3_ENABLED           1
/* UART 4 configuration */
#define UART4_MODE              MODE_DTE
#define UART4_IR                NO_IRDA
#define UART4_ENABLED           0	/* Disable UART 4 as its pins are shared with ATA */
/* UART 5 configuration */
#define UART5_MODE              MODE_DTE
#define UART5_IR                NO_IRDA
#define UART5_ENABLED           1


#define MXC_LL_UART_PADDR	UART1_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART1_BASE_ADDR)

/*!
 * @name Memory Size parameters
 */
/*! @{ */
/*!
 * Size of SDRAM memory
 */
#define SDRAM_MEM_SIZE          SZ_128M
/*!
 * Size of MBX buffer memory
 */
#define MXC_MBX_MEM_SIZE        SZ_16M
/*!
 * Size of memory available to kernel
 */
#define MEM_SIZE                (SDRAM_MEM_SIZE - MXC_MBX_MEM_SIZE)
/*! @} */

/*!
 * @name Keypad Configurations FIXME
 */
/*! @{ */
/*!
 * Maximum number of rows (0 to 7)
 */
#define MAXROW                  8
/*!
 * Maximum number of columns (0 to 7)
 */
#define MAXCOL                  8
/*! @} */


#define MXC_PMIC_INT_LINE	IOMUX_TO_IRQ(MX31_PIN_GPIO1_3)

#define AHB_FREQ                133000000
#define IPG_FREQ                66500000


#endif				/* __ASM_ARCH_MXC_BOARD_MARIO_H__ */
