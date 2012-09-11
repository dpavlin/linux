/*
 * linux/drivers/char/fiona_keymap.c
 *
 * Copyright 2005, Lab126.com <nvaccaro@lab126.com>
 *
 * Keyboard map for the Fiona Platform
 */

#include "fiona_keymap.h"

/* Keyboard mapping for Fiona IOC keyboard */

unsigned char kbd_ioc_xlate_evt2[FIONA_KEYMAP_SIZE] =  {
    KEY_1, KEY_7, KEY_E, KEY_O, KEY_G, KEY_X, KEY_KATAKANA, KEY_HENKAN,     /* 0x00 - 0x07 */
    KEY_2, KEY_8, KEY_R, KEY_P, KEY_H, KEY_C, KEY_SEMICOLON, KEY_HIRAGANA,  /* 0x08 - 0x0F */
    KEY_3, KEY_9, KEY_T, KEY_A, KEY_J, KEY_V, KEY_PAGEDOWN, KEY_LEFTSHIFT,  /* 0X10 - 0X17 */
    KEY_4, KEY_0, KEY_Y, KEY_S, KEY_K, KEY_B, KEY_PAGEUP, KEY_MUHENKAN,     /* 0X18 - 0X1F */
    KEY_5, KEY_Q, KEY_U, KEY_D, KEY_L, KEY_N, KEY_DOT, KEY_SPACE,           /* 0x20 - 0x27 */
    KEY_6, KEY_W, KEY_I, KEY_F, KEY_Z, KEY_M, KEY_SLASH, KEY_SPACE,         /* 0X28 - 0X2F */
    KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_BACKSPACE, KEY_ENTER,                 /* 0X30 - 0X33 */
    KEY_COMPOSE, KEY_KPJPCOMMA, KEY_YEN,                                    /* 0X34 - 0X36 */

    KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE,                       /* 0x37 - 0x3F */
    KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE,                                 /* Not defined */
    
    KEY_HANJA, KEY_HANGUEL,                                                 /* 0X40 - 0X41 */
};

unsigned char kbd_ioc_xlate_evt3[FIONA_KEYMAP_SIZE] =  {
    KEY_1, KEY_7, KEY_E, KEY_O, KEY_G, KEY_X, KEY_LEFTALT, KEY_HENKAN,      /* 0x00 - 0x07 */
    KEY_2, KEY_8, KEY_R, KEY_P, KEY_H, KEY_C, KEY_SPACE, KEY_HIRAGANA,      /* 0x08 - 0x0F */
    KEY_3, KEY_9, KEY_T, KEY_A, KEY_J, KEY_V, KEY_PAGEDOWN, KEY_LEFTSHIFT,  /* 0X10 - 0X17 */
    KEY_4, KEY_0, KEY_Y, KEY_S, KEY_K, KEY_B, KEY_PAGEUP, KEY_MUHENKAN,     /* 0X18 - 0X1F */
    KEY_5, KEY_Q, KEY_U, KEY_D, KEY_L, KEY_N, KEY_KPJPCOMMA, KEY_SLASH,     /* 0x20 - 0x27 */
    KEY_6, KEY_W, KEY_I, KEY_F, KEY_Z, KEY_M, KEY_COMPOSE, KEY_SEMICOLON,   /* 0X28 - 0X2F */
    KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_BACKSPACE, KEY_ENTER,                 /* 0X30 - 0X33 */
    KEY_DOT, KEY_KATAKANA, KEY_YEN,                                         /* 0X34 - 0X36 */

    KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE,                       /* 0x37 - 0x3F */
    KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE,                                 /* Not defined */
    
    KEY_HANJA, KEY_HANGUEL,                                                 /* 0X40 - 0X41 */
};

unsigned char kbd_ioc_xlate_dvt1[FIONA_KEYMAP_SIZE] =  {
    KEY_1, KEY_7, KEY_E, KEY_O, KEY_G, KEY_X, KEY_LEFTALT, KEY_HENKAN,      /* 0x00 - 0x07 */
    KEY_2, KEY_8, KEY_R, KEY_P, KEY_H, KEY_C, KEY_SPACE, KEY_HIRAGANA,      /* 0x08 - 0x0F */
    KEY_3, KEY_9, KEY_T, KEY_A, KEY_J, KEY_V, KEY_PAGEDOWN, KEY_LEFTSHIFT,  /* 0X10 - 0X17 */
    KEY_4, KEY_0, KEY_Y, KEY_S, KEY_K, KEY_B, KEY_PAGEUP, KEY_MUHENKAN,     /* 0X18 - 0X1F */
    KEY_5, KEY_Q, KEY_U, KEY_D, KEY_L, KEY_N, KEY_KPJPCOMMA, KEY_SLASH,     /* 0x20 - 0x27 */
    KEY_6, KEY_W, KEY_I, KEY_F, KEY_Z, KEY_M, KEY_COMPOSE, KEY_KPSLASH,     /* 0X28 - 0X2F */
    KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_BACKSPACE, KEY_ENTER,                 /* 0X30 - 0X33 */
    KEY_DOT, KEY_KATAKANA, KEY_YEN,                                         /* 0X34 - 0X36 */

    KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE, KEY_NONE,                       /* 0x37 - 0x3B */
    
    KEY_DOWN, KEY_UP, KEY_LEFT, KEY_RIGHT,                                  /* 0x3C - 0x3F */
    KEY_HANJA, KEY_HANGUEL,                                                 /* 0x40 - 0x41 */
};

#define IS_SCROLL_WHEEL_MOTION_KEY(k)               \
    ((KEY_HANGUEL == (k)) ||  /* scroll up     */   \
     (KEY_HANJA   == (k)))    /* scroll down   */   \

#define IS_SCROLL_WHEEL_KEY(k)                      \
    (IS_SCROLL_WHEEL_MOTION_KEY(k) ||               \
    (KEY_HENKAN  == (k)))     /* scroll select */

#define IS_TRACKBALL_MOTION_KEY(k)                  \
    ((KEY_LEFT    == (k)) ||  /* trackball left */  \
     (KEY_RIGHT   == (k)) ||  /* trackball right */ \
     IS_SCROLL_WHEEL_MOTION_KEY(k))

#define IS_TRACKBALL_KEY(k)                         \
    (IS_TRACKBALL_MOTION_KEY(k) ||                  \
    (KEY_HENKAN  == (k)))     /* scroll select */

/*
 * kbd_ioc_xlate will point to the proper board keymap
 * Default to dvt1 until init code polls board rev and
 * sets accordingly.
 */
unsigned char *kbd_ioc_xlate = kbd_ioc_xlate_dvt1;

/* Keyboard Mapping Accessor Utility Routines */

/***************************************************************************
 * Routine:     keycode_to_rawcode
 * Purpose:     Used to convert a keycode (the keyboard/event queue type) to
 *              a rawcode (raw data received by IOC).
 * Paramters:   keycode - the keycode value (kbd/event queue item)
 * Returns:     -1 : Error, couldn't find keycode in table
 *              int >= 0 : Raw code for that particular key/button/scroll event
 * Assumptions: none
 ***************************************************************************/
int keycode_to_rawcode(int keycode)
{
    int kc = (keycode & 0x7F);
    int x;
    for (x=0; x<FIONA_KEYMAP_SIZE; x++)
    {
        if (kbd_ioc_xlate[x] == kc)
            return(x);
    }
    // If we're here, we didn't find the key
    return(-1);
}

EXPORT_SYMBOL(keycode_to_rawcode);

int rawcode_to_keycode(int rawcode)
{
    if ((rawcode >= 0) && (rawcode < FIONA_KEYMAP_SIZE))
        return(kbd_ioc_xlate[rawcode]);

    // If we're here, key was out-of-bounds
    return(-1);
}

EXPORT_SYMBOL(rawcode_to_keycode);
