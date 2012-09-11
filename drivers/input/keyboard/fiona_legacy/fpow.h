/******************************************************************
 *
 *  File:   fpow.h
 *
 *  Author: Nick Vaccaro <nvaccaro@lab126.com>
 *
 *  Date:   05/10/06
 *
 * Copyright 2006, Lab126, Inc.  All rights reserved.
 *
 *  Description:
 *      Header file for internal Fiona Power Driver use
 *
 ******************************************************************/
#ifndef __FPOW_H__
#define __FPOW_H__

#include <linux/autoconf.h>

#ifdef __KERNEL__
#include <linux/miscdevice.h>
#include "fiona_power.h"
#include "fiona.h"
#endif

/****************************************
 *
 * Power related constants
 *
 ****************************************/

//////////////////////////////////////
// Various Debug-related switches
//////////////////////////////////////
#undef DEBUG_POWER_FIFOS
#undef DEBUG_POWER_REGISTRY

// Define to freeze processes from FPOW, not standard Linux pm.
#define FPOW_DO_PROCESS_SUSPEND_RESUME

//
// If the RTC driver receives an alarm request for the past,
// it will take the current time and add the following constant
// to assure that the device does in fact wake at some point
// in the future.  This is a safeguard mechanism.
//
#define RTC_TIME_1_HR_DELAY         (3600)
#define RTC_TIME_ERROR_WAKE_DELAY   RTC_TIME_1_HR_DELAY
#define RTC_TIME_23_HR_DELAY        (RTC_TIME_1_HR_DELAY*23)
#define RTC_TIME_24_HR_DELAY        (RTC_TIME_1_HR_DELAY*24)


//
// Turn on general PDEBUG if any of the debug modes is enabled
//
#if defined(DEBUG_POWER_FIFOS) || defined(DEBUG_POWER_REGISTRY)
#define PDEBUG(args...) printk(args)
#else
#define PDEBUG(args...) do {} while(0)
#endif

//
// Handy macros to make state names make sense !!
//
#define FPOW_STATE_ON           FPOW_STATE_D
#define FPOW_STATE_OFF          FPOW_STATE_M
#define FPOW_OFF_WAKE_STATE     FPOW_STATE_N
#define FPOW_IDLE_WAKE_STATE    FPOW_STATE_WW
#define FPOW_STARTING_STATE     FPOW_STATE_D
#define POWER_SWITCH_OFF_STATE  FPOW_STATE_M

//
// Define mechanism for dynamic enabling/disabling
//

extern int g_fiona_power_debug;
extern int g_fiona_event_debug;
extern int g_fiona_voltage_debug;
extern int g_fiona_vtable_debug;

#define powdebug(x...)  if (g_fiona_power_debug) printk(x)
#define eventdebug(x...)  if (g_fiona_event_debug) printk(x)
#define waterLog(x...)  if (g_fiona_voltage_debug) printk(x)
#define poweventdebug(x...) if (g_fiona_power_debug || g_fiona_event_debug) printk(x)

#undef EXTENSIVE_POWER_DEBUGGING
#ifdef EXTENSIVE_POWER_DEBUGGING
  #define extpowdebug(x...)   powdebug(x)
#else
  #define extpowdebug(x...)   do {} while (0)
#endif

// OP Amp Offset Values
#define OP_AMP_OFFSET_CALCULATION_SAMPLE_COUNT  20
#define MAMP_DRAW_WHILE_IDLE                    153     // actual value ~152.8, 153.2 w/ SDCard
#define MIN_MVOLT_VALUE                         3200    // At 3.2, HW shuts off, so we'd never see lower
#define MAX_MVOLT_VALUE                         4400    // Battery can swing higher than 4.2...
#define MIN_MAMP_VALUE                          25      // Min amperage while device up-and-running
#define MAX_MAMP_VALUE                          1500    // Max draw including WAN is 1300, so 1500 is good.

#define VOLTAGE_REFERENCE_BASE                  1000    // mv reference used for vref correction
#define MIN_VOLTAGE_REFERENCE_VALUE             975     // 2.05/2.1 = 976/1000
#define MAX_VOLTAGE_REFERENCE_VALUE             1025    // 2.15/2.1 = 1024/1000

#define MIN_TEST_VOLTAGE_REFERENCE_VALUE        950
#define MAX_TEST_VOLTAGE_REFERENCE_VALUE        1050

#define OP_AMP_GAIN_BASE                        1000    // Op amp is supposed to be 50x
#define MIN_GAIN_CORRECTION_VALUE               400
#define MAX_GAIN_CORRECTION_VALUE               1600

// Handy macros
#define memclr(a,l) memset(a,0,l)
#define MIN(x,y)    ((x<y)?x:y)
#define MAX(x,y)    ((x>y)?x:y)

#define FPOW_GLOBALS_SIGNATURE              (0x66706F77)    // 'fpow'

//
// Device Power Mode Constants
//
// (NOTE:  These constants are referred to in the JNI "misc.Util" library.
// If they are changed here, please make sure to update the JNI code as well!)
typedef enum FPOW_POWER_MODE {
    FPOW_MODE_OFF = 0,
    FPOW_MODE_ON,
    FPOW_MODE_OFF_NO_WAKE,
    FPOW_MODE_400_MHZ,
    FPOW_MODE_200_MHZ,
    FPOW_MODE_SLEEP,
} FPOW_POWER_MODE;
#define FPOW_MODE_FIRST 	FPOW_MODE_OFF
#define FPOW_MODE_LAST  	FPOW_MODE_SLEEP
#define FPOW_MODE_INVALID	0xFF

//
// Device Supports Mode Flags - these are bitfields
//
#define FPOW_MODE_ON_SUPPORTED                  (1<<FPOW_MODE_ON)
#define FPOW_MODE_SLEEP_SUPPORTED               (1<<FPOW_MODE_SLEEP)
#define FPOW_MODE_OFF_SUPPORTED                 (1<<FPOW_MODE_OFF)
#define FPOW_MODE_400_MHZ_SUPPORTED             (1<<FPOW_MODE_400_MHZ)
#define FPOW_MODE_200_MHZ_SUPPORTED             (1<<FPOW_MODE_200_MHZ)



//
// Power Component Classes
//
typedef enum FPOW_POWER_CLASS {
    FPOW_CLASS_VIDEO_DISPLAY = 0,
    FPOW_CLASS_VIDEO_PNLCD,
    FPOW_CLASS_WAN,
    FPOW_CLASS_USB_HOST,
    FPOW_CLASS_USB_DEVICE,
    FPOW_CLASS_MMC,
    FPOW_CLASS_AUDIO,
    FPOW_CLASS_KBD_BUTTONS,
    FPOW_CLASS_SCROLLWHEEL,
    FPOW_CLASS_IOC,
    FPOW_CLASS_CPU,
    FPOW_TOTAL_NUM_CLASSES
} FPOW_POWER_CLASS;
#define FPOW_CLASS_FIRST    FPOW_CLASS_VIDEO_DISPLAY
#define FPOW_CLASS_LAST     FPOW_CLASS_CPU
#define FPOW_CLASS_INVALID  0xFF

// Wake source PEDR bit definitions
#define WAN_WAKE_PEDR_BIT       PEDR_GPIO(g_fiona_hw.wake_on_wan_gpio)
#define WAN_SWITCH_PEDR_BIT     PEDR_GPIO(g_fiona_hw.wan_switch_gpio)
#define POWER_SWITCH_PEDR_BIT   PEDR_GPIO(g_fiona_hw.sleep_switch_gpio)
#define USB_WAKE_PEDR_BIT       PEDR_GPIO(g_fiona_hw.usb_wake_gpio)
#define IOC_PEDR_BIT            PEDR_GPIO(g_fiona_hw.ioc_wake_gpio)

//
// Callback-related structures
//
//typedef int (*fpow_get_power_mode_proc)(struct device *dev);
//typedef int (*fpow_set_power_mode_proc)(struct device *dev, u32 state, u32 level);
typedef int (*fpow_get_power_mode_proc)(void *private);
typedef FPOW_ERR (*fpow_set_power_mode_proc)(void *private, u32 state, u32 mode);

typedef struct fpow_component {
    char    name[FPOW_MAX_DEVICE_NAME_LENGTH];
    int     device_class;       // Class of device
    int     supported_modes;    // FPOW_POWER_MODES supported by this device
    void    *private;           // Private storage for device use
    fpow_get_power_mode_proc    getmode;   // Call to get current power mode
    fpow_set_power_mode_proc    setmode;   // Callback to set current power mode

    // List entries
    struct list_head            entry;      // Registery List Entry
    struct proc_dir_entry       *proc_fpow_component_entry;
}fpow_component;


typedef struct fpow_registration_rec {
    char    name[FPOW_MAX_DEVICE_NAME_LENGTH];
    int     device_class;       // Class of device
    int     supported_modes;    // FPOW_POWER_MODES supported by this device
    void    *private;           // Private storage for device use
    fpow_get_power_mode_proc    getmode;   // Call to get current power mode
    fpow_set_power_mode_proc    setmode;   // Callback to set current power mode
}fpow_registration_rec;



typedef enum FPOW_SNIFF_STATES {
    OFF_STATE_SNIFF = 0,
    ON_STATE_SNIFF,
    IDLE_SLEEP_STATE_SNIFF,
    POWER_LOCKOUT_STATE_SNIFF,
    WAKE_FROM_SLEEP_STATE_SNIFF,
    IDLE_SLEEP_OFF_FOUND,
} FPOW_SNIFF_STATES;

#endif  // __FPOW_H__

