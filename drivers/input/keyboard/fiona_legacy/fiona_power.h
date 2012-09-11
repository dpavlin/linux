/******************************************************************
 *
 *  File:   fiona_power.h
 *
 *  Author: Nick Vaccaro <nvaccaro@lab126.com>
 *
 *  Date:   05/05/06
 *
 *  Copyright 2006, Lab126, Inc.  All rights reserved.
 *
 *  Description:
 *      Holds APIs for the Fiona Power Driver
 *
 ******************************************************************/
#ifndef __FIONA_POWER_H__
#define __FIONA_POWER_H__

#include "fiona_events.h"

/****************************************
 *
 * Power related constants
 *
 ****************************************/
#define FPOW_MAJOR                          10  // Power driver major number
#define FPOW_MAX_DEVICE_NAME_LENGTH         32
#define FPOW_IOCTL_MAGIC_NUMBER             'p'
#define FPOW_DRIVER_PATH                    "/dev/misc/fpow"
#define FPOW_DRIVER_NAME                    "fpow"
#define FPOW_PROC_PARENT_NAME               "fpow"
#define FPOW_PROC_COMPONENTS_PARENT_NAME    "components"
#define FPOW_PROC_ENTRY_NAME                "fpow"
#define FPOW_PROC_WATERMARKS_PARENT_NAME    "watermarks"
#define FPOW_PROC_WAN_WATERMARK_NAME        "wan"
#define FPOW_PROC_LOW_WATERMARK_NAME        "low"
#define FPOW_PROC_CRITICAL_WATERMARK_NAME   "critical"
#define FPOW_PROC_UC_PARENT_NAME            "uc"
#define FPOW_PROC_UC_POWER_ENTRY_NAME       "power_switch"
#define FPOW_PROC_UC_WAN_ENTRY_NAME         "wan_switch"
#define FPOW_PROC_UC_HP_ENTRY_NAME          "headphones"
#define FPOW_PROC_UC_AC_ADAPTER_ENTRY_NAME  "ac_adapter"
#define FPOW_PROC_DEBUG_ENTRY_NAME          "debug"
#define FPOW_PROC_VDEBUG_ENTRY_NAME         "volt_debug"
#define FPOW_PROC_VTBL_DEBUG_ENTRY_NAME     "vtbl_debug"
#define FPOW_PROC_EVENT_DEBUG_ENTRY_NAME    "event_debug"
#define FPOW_PROC_SNIFF_DEBUG_ENTRY_NAME    "sniff_debug"
#define FPOW_PROC_REG_LIST_ENTRY_NAME       "reg_list"
#define FPOW_PROC_VERSION_ENTRY_NAME        "version"
#define FPOW_PROC_POIQ_ENTRY_NAME           "poiq"
#define FPOW_PROC_ACIEIQ_ENTRY_NAME         "acieiq"
#define FPOW_PROC_PEIQ_ENTRY_NAME           "peiq"
#define FPOW_PROC_FUSEQ_ENTRY_NAME          "fuseq"
#define FPOW_PROC_FUEQPL_ENTRY_NAME         "fueqpl"
#define FPOW_PROC_USEQ_ENTRY_NAME           "useq"
#define FPOW_PROC_CUEQ_ENTRY_NAME           "cueq"
#define FPOW_PROC_SEQ_ENTRY_NAME            "seq"
#define FPOW_PROC_SYSTEM_STATE_ENTRY_NAME   "state"
#define FPOW_PROC_EVENT_ENTRY_NAME          "event"
#define FPOW_PROC_EVENT_RAW_ENTRY_NAME      "event_raw"
#define FPOW_PROC_EVENT_TEST_ENTRY_NAME     "event_test"
#define FPOW_PROC_CACHE_ENTRY_NAME          "cache"
#define FPOW_PROC_WAKE_ENTRY_NAME           "wake"
#define FPOW_PROC_LAST_EVENT_ENTRY_NAME     "last_event"

#define FPOW_PROC_IWEIQ_ENTRY_NAME          "iweiq"
#define FPOW_PROC_IWCUEQ_ENTRY_NAME         "iwcueq"
#define FPOW_PROC_DIALOG_UP_ENTRY_NAME      "dialog_up"

//
// If doing the voltage checking on the host (DO_LOW_VOLTAGE_DETECTION_ON_HOST),
// VC_TIMER_FREQ defines the period of the voltage sampling timer (in ioc.c)
//
#define VC_TIMER_FREQ                       (10 * HZ)
#define VC_MIN_TIMER_FREQ                   (HZ * 1)


//
// System Power State Constants
//
typedef enum FPOW_POWER_STATE {

    FPOW_STATE_A = 0,   // Active / A1
    FPOW_STATE_B,       // Active / A2
    FPOW_STATE_C,       // Active / B1
    FPOW_STATE_D,       // Active / B2
    FPOW_STATE_E,       // Passive / PA1
    FPOW_STATE_F,       // Passive / PA2
    FPOW_STATE_G,       // Passive / PB1
    FPOW_STATE_H,       // Passive / PB2
    FPOW_STATE_I,       // Rest / RA1 w/ KeyHold
    FPOW_STATE_J,       // USB Temporary Wake State w/o WAN
    FPOW_STATE_K,       // USB Temporary Wake State w WAN
    FPOW_STATE_L,       // Rest / RA2 w/ KeyHold
    FPOW_STATE_M,       // Rest / Off
    FPOW_STATE_N,       // Temporary just-woke-from-off-sleep state
    FPOW_STATE_O,       // Password Entry w/ WAN w/o Audio
    FPOW_STATE_P,       // Password Entry w/o WAN w/o Audio
    FPOW_STATE_Q,       // Password Entry w/ WAN w/ Audio
    FPOW_STATE_R,       // Password Entry w/o WAN w/ Audio
    FPOW_STATE_S,       // USB Mounted w/out WAN
    FPOW_STATE_T,       // OFF - No Power
    FPOW_STATE_U,       // DEAD
    FPOW_STATE_V,       // Power Lockout
    FPOW_STATE_W,       // Passive / TPH wake state, ALT+FONT screen
    FPOW_STATE_X,       // USB Mounted w/ WAN
    FPOW_STATE_Y,       // Rest State w/out WAN w/out screen content clear
    FPOW_STATE_Z,       // Rest State w/ WAN w/out screen content clear
    FPOW_STATE_WW,      // Temporary just-woke-from-idle-sleep state
    FPOW_STATE_XX,      // USB Temporary Faded Wake State w WAN
    FPOW_STATE_YY,      // USB Temporary Faded Wake State w/o WAN
    FPOW_STATE_ZZ       // Passive TPH Wake State, Faded ALT+FONT screen

} FPOW_POWER_STATE;

#define FPOW_STATE_FIRST        FPOW_STATE_A
#define FPOW_STATE_LAST         FPOW_STATE_ZZ
#define FPOW_TOTAL_NUM_STATES   (FPOW_STATE_LAST-FPOW_STATE_FIRST+1)
#define FPOW_STATE_INVALID	    0xFF


typedef enum FPOW_WAKE_SOURCE {
    WAKE_INVALID            = INVALID_EVENT,
    WAN_RING_WAKE           = WAN_RING_EVENT,
    POWER_ON_WAKE           = POWER_SWITCH_ON_EVENT,
    POWER_OFF_WAKE          = POWER_SWITCH_OFF_EVENT,
    WAN_ON_WAKE             = WAN_SWITCH_ON_EVENT,
    WAN_OFF_WAKE            = WAN_SWITCH_OFF_EVENT,
    KEY_PRESS_WAKE          = KEYLOCK_EVENT,
    AC_PLUG_INSERTED_WAKE   = AC_PLUG_INSERTED_EVENT,
    LOW_BATTERY_WAKE        = LOW_BATTERY_EVENT,
    WATERMARK_WAKE          = WATERMARK_CHECK_EVENT,
    USB_WAKE                = USB_WAKE_EVENT,

    /* the following two are only used beneath drivers */
    PXA_UART_WAKE            = FPOW_NUM_OF_EVENTS+1,
    IOC_WAKE                 = FPOW_NUM_OF_EVENTS+2,
    ALT_FONT_KEY_COMBINATION = FPOW_NUM_OF_EVENTS+3,
} FPOW_WAKE_SOURCE;


//
// FPOW_SET_STATE_IOCTL error responses
//
typedef enum    FPOW_ERR    {
    FPOW_NO_ERR = 0,
    FPOW_CANCELLED_EVENT_ERR,
    FPOW_COMPONENT_TRANSITION_ERR,
    FPOW_NOT_ENOUGH_POWER_ERR,
    FPOW_COMPONENT_NOT_POWERED_ERR,
    FPOW_NO_MATCHES_FOUND_ERR
} FPOW_ERR;

//
// IOCTL Structures
//
typedef struct FPOW_POWER_STATE_REC {
    FPOW_POWER_STATE state;
}FPOW_POWER_STATE_REC;

typedef struct FPOW_WAKE_SRC_REC {
    FPOW_WAKE_SOURCE source;
}FPOW_WAKE_SRC_REC;

//
// IOCTL Constants
//
#define FPOW_SET_STATE_IOCTL                    _IOW(FPOW_IOCTL_MAGIC_NUMBER, 0, FPOW_POWER_STATE_REC *)
#define FPOW_GET_STATE_IOCTL                    _IOR(FPOW_IOCTL_MAGIC_NUMBER, 1, FPOW_POWER_STATE_REC *)
#define FPOW_GET_WAKE_SRC_IOCTL                 _IOR(FPOW_IOCTL_MAGIC_NUMBER, 2, FPOW_WAKE_SRC_REC *)
#define FPOW_CLEAR_DISABLE_VIDEO_IOCTL          _IOW(FPOW_IOCTL_MAGIC_NUMBER, 3, void)
#define FPOW_ENABLE_VIDEO_IOCTL                 _IOW(FPOW_IOCTL_MAGIC_NUMBER, 4, void)
#define FPOW_DISPLAY_FAST_SCREEN_IOCTL          _IOW(FPOW_IOCTL_MAGIC_NUMBER, 5, int)
#define FPOW_DISPLAY_FAST_SCREEN_SLEEP_IOCTL    _IOW(FPOW_IOCTL_MAGIC_NUMBER, 6, int)
#define FPOW_IOCTLLast          6

#endif  // __FIONA_POWER_H__
