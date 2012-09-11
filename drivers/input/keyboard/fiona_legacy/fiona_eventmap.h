#ifndef __FIONA_EVENTMAP_H__
#define __FIONA_EVENTMAP_H__

#include <linux/input.h>
#include "fiona_events.h"

// Translation Table
extern unsigned char fiona_event_xlate[];

// Event codes for the trackball and trackball-like-device events
#define TRACKBALL_EVENTCODE_PRESSED     0x07 // same as scrollwheel pressed
#define TRACKBALL_EVENTCODE_RELEASED    (TRACKBALL_EVENTCODE_PRESSED  | 0x80)

#define TRACKBALL_EVENTCODE_PRESSED2    0x3C // same as scrollwheel pressed
#define TRACKBALL_EVENTCODE_RELEASED2   (TRACKBALL_EVENTCODE_PRESSED2 | 0x80)

#define TRACKBALL_EVENTCODE_HELD        0x3D // extended TRACKBALL_EVENTCODE_PRESSED
#define TRACKBALL_EVENTCODE_HELD2       (TRACKBALL_EVENTCODE_HELD | 0x80)

#define TRACKBALL_EVENTCODE_LEFT        0x3E // unique to trackball
#define TRACKBALL_EVENTCODE_RIGHT       0x3F // unique to trackball
#define TRACKBALL_EVENTCODE_DOWN        0x40 // same as scrollwheel down
#define TRACKBALL_EVENTCODE_UP          0x41 // same as scrollwheel up

#define TRACKBALL_EVENTCODE_LEFT2       (TRACKBALL_EVENTCODE_LEFT  | 0x80)
#define TRACKBALL_EVENTCODE_RIGHT2      (TRACKBALL_EVENTCODE_RIGHT | 0x80)
#define TRACKBALL_EVENTCODE_DOWN2       (TRACKBALL_EVENTCODE_DOWN  | 0x80)
#define TRACKBALL_EVENTCODE_UP2         (TRACKBALL_EVENTCODE_UP    | 0x80)

// Event Codes That Are Used By Kernel Code
#define BATTERY_LOW_EVENTCODE           0x42
#define BATTERY_NO_WAN_EVENTCODE        (BATTERY_LOW_EVENTCODE | 0x80)
#define BATTERY_CRITICAL_EVENTCODE      0x43
#define WATERMARK_CHECK_EVENTCODE       (BATTERY_CRITICAL_EVENTCODE | 0x80)
#define POWER_SW_ON_EVENTCODE           0x44
#define POWER_SW_OFF_EVENTCODE          (POWER_SW_ON_EVENTCODE  | 0x80)
#define WAN_SW_ON_EVENTCODE             0x45
#define WAN_SW_OFF_EVENTCODE            (WAN_SW_ON_EVENTCODE    | 0x80)
#define AC_IN_EVENTCODE                 0x46
#define AC_OUT_EVENTCODE                (AC_IN_EVENTCODE        | 0x80)
#define USB_IN_EVENTCODE                0x47
#define USB_OUT_EVENTCODE               (USB_IN_EVENTCODE       | 0x80)
#define HEADPHONES_IN_EVENTCODE         0x48
#define HEADPHONES_OUT_EVENTCODE        (HEADPHONES_IN_EVENTCODE| 0x80)
#define WAN_RING_EVENTCODE              0x49
#define TPH_COMPLETE_EVENTCODE          (WAN_RING_EVENTCODE | 0x80)

#define KEY_WAKE_EVENTCODE              0x4A
#define USB_WAKE_EVENTCODE              (KEY_WAKE_EVENTCODE | 0x80)

#define AUDIO_ON_EVENTCODE              0x4B
#define AUDIO_OFF_EVENTCODE             (AUDIO_ON_EVENTCODE | 0x80)
#define MMC_EVENTCODE                   0x4C
#define PRIME_PUMP_EVENTCODE            (MMC_EVENTCODE | 0x80)
#define VALID_PASSWORD_EVENTCODE        0x4D
#define INVALID_PASSWORD_EVENTCODE      (VALID_PASSWORD_EVENTCODE | 0x80)
#define TIMEOUT1_EVENTCODE              0x4E
#define TIMEOUT2_EVENTCODE              (TIMEOUT1_EVENTCODE  | 0x80)
#define TIMEOUT3_EVENTCODE              0x4F
#define TIMEOUT_USB_WAKE_EVENTCODE      (TIMEOUT3_EVENTCODE  | 0x80)

#define SWEEPER_EVENTCODE               0x50
#define WAN_SW_SWEEPER_EVENTCODE        SWEEPER_EVENTCODE
#define TWO_FINGER_SWEEPER_EVENTCODE    (SWEEPER_EVENTCODE | 0x80)

// When defining first and last, don't count the top bit...
#define FIONA_EVENT_FIRST               BATTERY_LOW_EVENTCODE
#define FIONA_EVENT_LAST                SWEEPER_EVENTCODE 
#define MAX_FIONA_EVENT_CODES           (FIONA_EVENT_LAST-FIONA_EVENT_FIRST+1)

void handle_fiona_event(unsigned char eventCode);
int get_fiona_event_code(int scancode);
char *fiona_event_keycode_to_str(int keycode);
int keycode_to_rawcode(int keycode);
int rawcode_to_keycode(int rawcode);

#endif // __FIONA_EVENTMAP_H__
