/*
 * linux/drivers/char/fiona_keymap.h
 *
 * Copyright 2005, Lab126.com <nvaccaro@lab126.com>
 *
 * Keyboard map constants for the Fiona Platform Keyboard
 */
#ifndef __FIONA_KEYMAP__
#define __FIONA_KEYMAP__

#include <linux/autoconf.h>

#define FIONA_KEYMAP_SIZE               0x42
#define KEY_NONE                        KEY_RESERVED

// Some handy key defs for identifying ALT+FONT key events
#define ALT_KEY_PRESSED     KEY_LEFTALT
#define ALT_KEY_RELEASED    (ALT_KEY_PRESSED | 0x80)
#define FONT_KEY_PRESSED    KEY_KATAKANA
#define FONT_KEY_RELEASED   (KEY_KATAKANA | 0x80)

// NOTE: due to the way some of the code uses AUX_BUF_SIZE, it's important that
// it's a power of 2 !!
#define AUX_BUF_SIZE       2048

typedef struct kbd_queue {
    unsigned long head;
    unsigned long tail;
    wait_queue_head_t proc_list;
    struct fasync_struct *fasync;
    unsigned char buf[AUX_BUF_SIZE];
}kbd_queue;

extern int keycode_to_rawcode(int keycode);
extern int rawcode_to_keycode(int rawcode);

#endif // __FIONA_KEYMAP__
