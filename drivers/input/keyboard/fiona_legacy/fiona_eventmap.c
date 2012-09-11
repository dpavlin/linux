/*
 * linux/drivers/char/fiona_eventmap.c
 *
 * Copyright 2005, 2006, Lab126.com <nvaccaro@lab126.com>
 *
 * Keyboard map for the Fiona Platform
 */

#include "fiona_eventmap.h"

unsigned char fiona_event_xlate[MAX_FIONA_EVENT_CODES] =  {
    EVENT_BATTERY_LOW, EVENT_BATTERY_CRITICAL, EVENT_POWER_SWITCH,
    EVENT_WAN_SWITCH, EVENT_AC, EVENT_USB, EVENT_HEADPHONES, EVENT_WAN_RING, 
    EVENT_KEY_WAKE, EVENT_AUDIO, EVENT_MMC, EVENT_VALID_PASSWORD, EVENT_TIMEOUT, 
    EVENT_ALT_TIMEOUTS, EVENT_SWEEPER
};

int get_fiona_event_code(int scancode)
{
    return(fiona_event_xlate[scancode-FIONA_EVENT_FIRST]);
}

char *fiona_event_keycode_to_str(int keycode)
{
    switch(keycode)  {
        case BATTERY_LOW_EVENTCODE:
            return("BATTERY_LOW");
        case BATTERY_NO_WAN_EVENTCODE:
            return("NO_WAN_BATTERY_LOW");
        case BATTERY_CRITICAL_EVENTCODE:
            return("BATTERY_CRITICAL");
        case POWER_SW_OFF_EVENTCODE:
            return("POWER_SWITCH_OFF");
        case POWER_SW_ON_EVENTCODE:
            return("POWER_SWITCH_ON");
        case AUDIO_OFF_EVENTCODE:
            return("AUDIO_OFF");
        case AUDIO_ON_EVENTCODE:
            return("AUDIO_ON");
        case WAN_SW_OFF_EVENTCODE:
            return("WAN_SWITCH_OFF");
        case WAN_SW_ON_EVENTCODE:
            return("WAN_SWITCH_ON");
        case WAN_SW_SWEEPER_EVENTCODE:
            return("WAN_SWITCH_SWEEPER");
        case TWO_FINGER_SWEEPER_EVENTCODE:
            return("TWO_FINGER_SWEEPER");
        case TIMEOUT_USB_WAKE_EVENTCODE:
            return("TIMEOUT_USB_WAKE");
        case AC_OUT_EVENTCODE:
            return("AC_ADAPTER_REMOVED");
        case AC_IN_EVENTCODE:
            return("AC_ADAPTER_INSERTED");
        case USB_OUT_EVENTCODE:
            return("USB_REMOVED");
        case USB_WAKE_EVENTCODE:
            return("USB_WAKE");
        case USB_IN_EVENTCODE:
            return("USB_INSERTED");
        case HEADPHONES_OUT_EVENTCODE:
            return("HEADPHONES_REMOVED");
        case HEADPHONES_IN_EVENTCODE:
            return("HEADPHONES_INSERTED");
        case WAN_RING_EVENTCODE:
            return("WAN_RING");
        case TPH_COMPLETE_EVENTCODE:
            return("TPH_COMPLETE");
        case KEY_WAKE_EVENTCODE:
            return("KEY_WAKE");
        case VALID_PASSWORD_EVENTCODE:
            return("VALID_PASSWORD_EVENT");
        case INVALID_PASSWORD_EVENTCODE:
            return("INVALID_PASSWORD_EVENT");
        case WATERMARK_CHECK_EVENTCODE:
            return("WATERMARK_CHECK_EVENT");
        case PRIME_PUMP_EVENTCODE:
            return("PRIME_PUMP_EVENT");
        case MMC_EVENTCODE:
            return("MMC_EVENT");
        case TIMEOUT1_EVENTCODE:
            return("TIMEOUT1_EVENT");
        case TIMEOUT2_EVENTCODE:
            return("TIMEOUT2_EVENT");
        case TIMEOUT3_EVENTCODE:
            return("TIMEOUT3_EVENT");
        default:
            return("UNKNOWN");
    }
}
