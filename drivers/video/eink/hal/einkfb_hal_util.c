/*
 *  linux/drivers/video/eink/hal/einkfb_hal_util.c -- eInk frame buffer device HAL utilities
 *
 *      Copyright (C) 2005-2009 Lab126
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include "einkfb_hal.h"

#if PRAGMAS
    #pragma mark Definitions
    #pragma mark -
#endif

extern einkfb_power_level EINKFB_GET_POWER_LEVEL(void);
extern void EINKFB_SET_POWER_LEVEL(einkfb_power_level power_level);

// 1bpp -> 2bpp
//
// 0 0 0 0 -> 00 00 00 00   0x0 -> 0x00
// 0 0 0 1 -> 00 00 00 11   0x1 -> 0x03
// 0 0 1 0 -> 00 00 11 00   0x2 -> 0x0C
// 0 0 1 1 -> 00 00 11 11   0x3 -> 0x0F
// 0 1 0 0 -> 00 11 00 00   0x4 -> 0x30
// 0 1 0 1 -> 00 11 00 11   0x5 -> 0x33
// 0 1 1 0 -> 00 11 11 00   0x6 -> 0x3C
// 0 1 1 1 -> 00 11 11 11   0x7 -> 0x3F
// 1 0 0 0 -> 11 00 00 00   0x8 -> 0xC0
// 1 0 0 1 -> 11 00 00 11   0x9 -> 0xC3
// 1 0 1 0 -> 11 00 11 00   0xA -> 0xCC
// 1 0 1 1 -> 11 00 11 11   0xB -> 0xCF
// 1 1 0 0 -> 11 11 00 00   0xC -> 0xF0
// 1 1 0 1 -> 11 11 00 11   0xD -> 0xF3
// 1 1 1 0 -> 11 11 11 00   0xE -> 0xFC
// 1 1 1 1 -> 11 11 11 11   0xF -> 0xFF
//
static u8 stretch_nybble_table_1_to_2bpp[16] =
{
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

// 2bpp -> 4bpp
//
// 00 00 -> 0000 0000       0x0 -> 0x00
// 00 01 -> 0000 0101       0x1 -> 0x05
// 00 10 -> 0000 1010       0x2 -> 0x0A
// 00 11 -> 0000 1111       0x3 -> 0x0F
// 01 00 -> 0101 0000       0x4 -> 0x50
// 01 01 -> 0101 0101       0x5 -> 0x55
// 01 10 -> 0101 1010       0x6 -> 0x5A
// 01 11 -> 0101 1111       0x7 -> 0x5F
// 10 00 -> 1010 0000       0x8 -> 0xA0
// 10 01 -> 1010 0101       0x9 -> 0xA5
// 10 10 -> 1010 1010       0xA -> 0xAA
// 10 11 -> 1010 1111       0xB -> 0xAF
// 11 00 -> 1111 0000       0xC -> 0xF0
// 11 01 -> 1111 0101       0xD -> 0xF5
// 11 10 -> 1111 1010       0xE -> 0xFA
// 11 11 -> 1111 1111       0xF -> 0xFF
//
static u8 stretch_nybble_table_2_to_4bpp[16] =
{
    0x00, 0x05, 0x0A, 0x0F, 0x50, 0x55, 0x5A, 0x5F,
    0xA0, 0xA5, 0xAA, 0xAF, 0xF0, 0xF5, 0xFA, 0xFF
};

// 4bpp -> 8bpp
//
// 0000 -> 00000000         0x0 -> 0x00
// 0001 -> 00010001         0x1 -> 0x11
// 0010 -> 00100010         0x2 -> 0x22
// 0011 -> 00110011         0x3 -> 0x33
// 0100 -> 01000100         0x4 -> 0x44
// 0101 -> 01010101         0x5 -> 0x55
// 0110 -> 01100110         0x6 -> 0x66
// 0111 -> 01110111         0x7 -> 0x77
// 1000 -> 10001000         0x8 -> 0x88
// 1001 -> 10011001         0x9 -> 0x99
// 1010 -> 10101010         0xA -> 0xAA
// 1011 -> 10111011         0xB -> 0xBB
// 1100 -> 11001100         0xC -> 0xCC
// 1101 -> 11011101         0xD -> 0xDD
// 1110 -> 11101110         0xE -> 0xEE
// 1111 -> 11111111         0xF -> 0xFF
//
static u8 stretch_nybble_table_4_to_8bpp[16] =
{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// 1bpp
//
//  0   0 0 0 0 0 0 0 0     0x00
//  1   1 1 1 1 1 1 1 1     0xFF
//
static u8 gray_table_1bpp[2] =
{
    0x00, 0xFF
};

// 2bpp
//
//  0   00 00 00 00         0x00
//  1   01 01 01 01         0x55
//  2   10 10 10 10         0xAA
//  3   11 11 11 11         0xFF
//
static u8 gray_table_2bpp[4] =
{
    0x00, 0x55, 0xAA, 0xFF
};

// 4bpp
//
//  0   0000 0000           0x00
//  1   0001 0001           0x11
//  2   0010 0010           0x22
//  3   0011 0011           0x33
//  4   0100 0100           0x44
//  5   0101 0101           0x55
//  6   0110 0110           0x66
//  7   0111 0111           0x77    
//  8   1000 1000           0x88
//  9   1001 1001           0x99
// 10   1010 1010           0xAA
// 11   1011 1011           0xBB
// 12   1100 1100           0xCC
// 13   1101 1101           0xDD
// 14   1110 1110           0xEE
// 15   1111 1111           0xFF
//
static u8 gray_table_4bpp[16] =
{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

einkfb_power_level saved_power_level = einkfb_power_level_init;
static DECLARE_MUTEX(einkfb_lock);

#if PRAGMAS
    #pragma mark -
    #pragma mark Local Utilites
    #pragma mark -
#endif

struct vfb_blit_t
{
    u8 *src, *dst;
};
typedef struct vfb_blit_t vfb_blit_t;

static void einkfb_vfb_blit(int x, int y, int rowbytes, int bytes, void *data)
{
    vfb_blit_t *vfb_blit = (vfb_blit_t *)data;
    vfb_blit->dst[(rowbytes * y) + x] = vfb_blit->src[bytes];
}

static void einkfb_update_vfb_area(update_area_t *update_area)
{
    // If we get here, the update_area has already been validated.  So, all we
    // need to do is blit things into the virtual framebuffer at the right
    // spot.
    //
    u32 x_start, x_end, area_xres,
        y_start, y_end, area_yres;

    u8  *fb_start, *area_start;
    
    vfb_blit_t vfb_blit;
    
    struct einkfb_info info;
    einkfb_event_t event;
    
    einkfb_init_event(&event);
    einkfb_get_info(&info);
    
    area_xres  = update_area->x2 - update_area->x1;
    area_yres  = update_area->y2 - update_area->y1;
    area_start = update_area->buffer;
    
    fb_start   = info.vfb;

    // Get the (x1, y1)...(x2, y2) offset into the framebuffer.
    //
    x_start = update_area->x1;
    x_end   = x_start + area_xres;
    
    y_start = update_area->y1;
    y_end   = y_start + area_yres;
    
    // Blit the area data into the virtual framebuffer.
    //
    vfb_blit.src = area_start;
    vfb_blit.dst = fb_start;
    
    einkfb_blit(x_start, x_end, y_start, y_end, einkfb_vfb_blit, (void *)&vfb_blit);

    // Say that an update-display event has occurred.
    //
    event.update_mode = update_area->which_fx;
    event.x1 = update_area->x1; event.x2 = update_area->x2;
    event.y1 = update_area->y1; event.y2 = update_area->y2;
    event.event = einkfb_event_update_display_area;
    
    einkfb_post_event(&event);
}

static void einkfb_update_vfb(fx_type update_mode)
{
    fb_apply_fx_t fb_apply_fx = get_fb_apply_fx();
    struct einkfb_info info;
    einkfb_event_t event;
    
    einkfb_init_event(&event);
    einkfb_get_info(&info);
    
    // Copy the real framebuffer into the virtual framebuffer if we're
    // not doing a restore.
    //
    if ( !EINKFB_RESTORE(info) )
    {    
        if ( fb_apply_fx )
        {
            int i;
            
            for ( i = 0; i < info.size; i++ )
            {
                info.vfb[i] = fb_apply_fx(info.start[i], i);
                EINKFB_SCHEDULE_BLIT(i+1);
            }
        }
        else
            EINKFB_MEMCPYK(info.vfb, info.start, info.size);
    }

    // Say that an update-display event has occurred.
    //
    event.event = einkfb_event_update_display;
    event.update_mode = update_mode;

    einkfb_post_event(&event);
}

#if PRAGMAS
    #pragma mark -
    #pragma mark Locking Utilities
    #pragma mark -
#endif

static int einkfb_begin_power_on_activity(void)
{
    int result = EINKFB_FAILURE;
    struct einkfb_info info;
    
    einkfb_get_info(&info);

    if ( info.dev )
    {
        // Save the current power level.
        //
        saved_power_level = EINKFB_GET_POWER_LEVEL();
           
        // Only power on if it's allowable to do so at the moment.
        //
        if ( POWER_OVERRIDE() )
        {
            result = EINKFB_SUCCESS;
        }
        else
        {
            switch ( saved_power_level )
            {
                case einkfb_power_level_init:
                    saved_power_level = einkfb_power_level_on;

                case einkfb_power_level_on:
                case einkfb_power_level_standby:
                    einkfb_prime_power_timer(EINKFB_DELAY_TIMER);
                case einkfb_power_level_blank:
                    result = EINKFB_SUCCESS;
                break;
                
                default:
                    einkfb_debug_power("power (%s) lockout, ignoring call\n",
                        einkfb_get_power_level_string(saved_power_level));
                    
                    result = EINKFB_FAILURE;
                break;
            }
        }

        // Power on if need be and allowable.
        //
        if ( EINKFB_SUCCESS == result )
        {
            einkfb_set_jif_on(jiffies);
            
            if ( einkfb_power_level_on != saved_power_level )
                EINKFB_SET_POWER_LEVEL(einkfb_power_level_on);
        }
    }
    
    return ( result );
}

static void einkfb_end_power_on_activity(void)
{
    struct einkfb_info info; einkfb_get_info(&info);

    if ( info.dev )
    {
        einkfb_power_level power_level = EINKFB_GET_POWER_LEVEL();
        
        // Only restore the saved power level if we haven't been purposely
        // taken out of the "on" level.
        //
        if ( einkfb_power_level_on == power_level )
            EINKFB_SET_POWER_LEVEL(saved_power_level);
        else
            saved_power_level = power_level;
    }
}

bool einkfb_lock_ready(bool release)
{
    bool ready = false;
    
    if ( release )
    {
        ready = EINKFB_SUCCESS == down_trylock(&einkfb_lock);
        
        if ( ready )
            einkfb_lock_release();
    }
    else
        ready = EINKFB_SUCCESS == down_interruptible(&einkfb_lock);
    
    return ( ready );
}

void einkfb_lock_release(void)
{
    up(&einkfb_lock);
}

int einkfb_lock_entry(char *function_name)
{
    int result = EINKFB_FAILURE;
    
    einkfb_debug_lock("%s: getting power lock...\n", function_name);

    if ( EINKFB_LOCK_READY() )
    {
        einkfb_debug_lock("%s: got lock, getting power...\n", function_name);
        result = einkfb_begin_power_on_activity();

        if ( EINKFB_SUCCESS == result )
        {
            einkfb_debug_lock("%s: got power...\n", function_name);
        }
        else
        {
            einkfb_debug_lock("%s: could not get power, releasing lock\n", function_name);
            EINFFB_LOCK_RELEASE();
        }
    }
    else
    {
        einkfb_debug_lock("%s: could not get lock, bailing\n", function_name);
    }

	return ( result );
}

void einkfb_lock_exit(char *function_name)
{
    einkfb_end_power_on_activity();
    EINFFB_LOCK_RELEASE();

    einkfb_debug_lock("%s released power, released lock\n", function_name);
}

int einkfb_schedule_timeout(unsigned long hardware_timeout, einkfb_hardware_ready_t hardware_ready, void *data)
{
    int result = EINKFB_SUCCESS;
    
    if ( hardware_timeout && hardware_ready )
    {
        unsigned long start_time = jiffies, stop_time = start_time + hardware_timeout,
            timeout = EINKFB_TIMEOUT_MIN;

        // Ask the hardware whether it's ready or not.  And, if it's not ready, start
        // yielding the CPU for EINKFB_TIMEOUT_MIN jiffies, increasing the yield time
        // up to EINKFB_TIMEOUT_MAX jiffies.  Time out after the requested number of
        // of jiffies have occurred.
        //
        while ( !(*hardware_ready)(data) && time_before_eq(jiffies, stop_time) )
            schedule_timeout_interruptible(min(timeout++, EINKFB_TIMEOUT_MAX));

        if ( !(*hardware_ready)(data) && time_after(jiffies, stop_time) )
        {
           einkfb_print_crit("Timed out waiting for the hardware to become ready!\n");
           result = EINKFB_FAILURE;
        }
        else
        {
            // For debugging purposes, dump the time it took for the hardware to
            // become ready if it was more than EINKFB_TIMEOUT_MAX.
            //
            stop_time = jiffies - start_time;
            
            if ( EINKFB_TIMEOUT_MAX < stop_time )
                einkfb_debug("Timeout time = %ld\n", stop_time);
        }
    }
    else
    {
        // Yield the CPU with schedule.
        //
        einkfb_debug("Yielding CPU with schedule.\n");
        schedule();
    }
    
    return ( result );
}

#if PRAGMAS
    #pragma mark -
    #pragma mark Display Utilities
    #pragma mark -
#endif

void einkfb_blit(int xstart, int xend, int ystart, int yend, einkfb_blit_t blit, void *data)
{
    if ( blit )
    {
        int x, y, rowbytes, bytes;
        
        struct einkfb_info info;
        einkfb_get_info(&info);
    
        // Make bpp-related adjustments.
        //
        xstart   = BPP_SIZE(xstart,    info.bpp);
        xend     = BPP_SIZE(xend,	   info.bpp);
        rowbytes = BPP_SIZE(info.xres, info.bpp);
    
        // Blit EINKFB_MEMCPY_MIN bytes at a time before yielding.
        //
        for (bytes = 0, y = ystart; y < yend; y++ )
        {
            for ( x = xstart; x < xend; x++ )
            {
                (*blit)(x, y, rowbytes, bytes, data);
                EINKFB_SCHEDULE_BLIT(++bytes);
            }
        }
    }
}

static einkfb_bounds_failure bounds_failure = einkfb_bounds_failure_none;

einkfb_bounds_failure einkfb_get_last_bounds_failure(void)
{
    return ( bounds_failure );
}

bool einkfb_bounds_are_acceptable(int xstart, int xres, int ystart, int yres)
{
    bool acceptable = true;
    int alignment;
    
    struct einkfb_info info;
    einkfb_get_info(&info);
    
    bounds_failure = einkfb_bounds_failure_none;
    alignment = BPP_BYTE_ALIGN(info.align);

    // The limiting boundary must be of non-zero size and be within the screen's boundaries.
    //
    acceptable &= ((0 < xres) && IN_RANGE(xres, 0, info.xres));
    
    if ( !acceptable )
        bounds_failure |= einkfb_bounds_failure_x1;

    acceptable &= ((0 < yres) && IN_RANGE(yres, 0, info.yres));
    
    if ( !acceptable )
        bounds_failure |= einkfb_bounds_failure_y1;
    
    // The bounds must be and fit within the framebuffer.
    //
    acceptable &= ((0 <= xstart) && ((xstart + xres) <= info.xres));

    if ( !acceptable )
        bounds_failure |= einkfb_bounds_failure_x2;

    acceptable &= ((0 <= ystart) && ((ystart + yres) <= info.yres));
    
    if ( !acceptable )
        bounds_failure |= einkfb_bounds_failure_y2;

    // Our horizontal starting and ending points must be byte aligned.
    //
    acceptable &= ((0 == (xstart % alignment)) && (0 == (xres % alignment)));
    
    if ( !acceptable )
        bounds_failure |= einkfb_bounds_failure_x;
        
    // If debugging is enabled, print out the apparently errant
    // passed-in values.
    //
    if ( !acceptable )
    {
        einkfb_debug("Image couldn't be rendered:\n");
        einkfb_debug(" Screen bounds:         %4d x %4d\n", info.xres, info.yres);
        einkfb_debug(" Image resolution:      %4d x %4d\n", xres, yres);
        einkfb_debug(" Image offset:          %4d x %4d\n", xstart, ystart);
        einkfb_debug(" Image row start align: %4d\n",       (xstart % alignment));
        einkfb_debug(" Image width align:     %4d\n",       (xres % alignment));
	}

	return ( acceptable );
}

bool einkfb_align_bounds(rect_t *rect)
{
    int xstart = rect->x1, xend = rect->x2,
        ystart = rect->y1, yend = rect->y2,
        
        xres = xend - xstart,
        yres = yend - ystart,
        
        alignment;
    
    struct einkfb_info info;
    bool aligned = false;
    
    einkfb_get_info(&info);
    alignment = BPP_BYTE_ALIGN(info.align);
    
    // Only re-align the bounds that aren't aligned.
    //
    if ( 0 != (xstart % alignment) )
    {
        xstart = BPP_BYTE_ALIGN_DN(xstart, info.align);
        xres = xend - xstart;
    }

    if ( 0 != (xres % alignment) )
    {
        xend = BPP_BYTE_ALIGN_UP(xend, info.align);
        xres = xend - xstart;
    }

    // If the re-aligned bounds are acceptable, use them.
    //
    if ( einkfb_bounds_are_acceptable(xstart, xres, ystart, yres) )
    {
        einkfb_debug("x bounds re-aligned, x1: %d -> %d; x2: %d -> %d\n",
            rect->x1, xstart, rect->x2, xend);
        
        rect->x1 = xstart;
        rect->x2 = xend;
        
        aligned = true;
    }
    
    return ( aligned );
}

unsigned char einkfb_stretch_nybble(unsigned char nybble, unsigned long bpp)
{
    unsigned char *which_nybble_table = NULL, result = nybble;

    switch ( nybble )
    {
        // Special-case the table endpoints since they're always the same.
        //
        case 0x00:
            result = EINKFB_WHITE;
        break;
        
        case 0x0F:
            result = EINKFB_BLACK;
        break;
        
        // Handle everything else on a bit-per-pixel basis.
        //
        default:
            switch ( bpp )
            {
                case EINKFB_1BPP:
                    which_nybble_table = stretch_nybble_table_1_to_2bpp;
                break;
                
                case EINKFB_2BPP:
                    which_nybble_table = stretch_nybble_table_2_to_4bpp;
                break;
                
                case EINKFB_4BPP:
                    which_nybble_table = stretch_nybble_table_4_to_8bpp;
                break;
            }
            
            if ( which_nybble_table )
                result = which_nybble_table[nybble];
        break;
    }

    return ( result );
}

void einkfb_display_grayscale_ramp(void)
{
    struct einkfb_info info;
    u8 *gray_table = NULL;
    int i, j, k, m, n;

    einkfb_get_info(&info);
    
    switch ( info.bpp )
    {
        case EINKFB_1BPP:
            gray_table = gray_table_1bpp;
        break;
        
        case EINKFB_2BPP:
            gray_table = gray_table_2bpp;
        break;
        
        case EINKFB_4BPP:
            gray_table = gray_table_4bpp;
        break;
    }
    
    // Cycle through the framebuffer, filling it with all possible
    // grays at the current bit depth.
    //
    for ( i = j = k = 0, m = (1 << info.bpp), n = info.size; i < n; i++ )
    {
        info.start[i] = gray_table ? gray_table[j] : (u8)j;
            
        if ( 0 == (++k % (n/m)) )
        {
            k = 0;
            j++;
        }

        EINKFB_SCHEDULE_BLIT(i+1);
    }
    
    // Now, display the ramp.
    //
    einkfb_update_display(fx_update_full);
}

#if PRAGMAS
    #pragma mark -
    #pragma mark Update Utilities
    #pragma mark -
#endif

void einkfb_update_display_area(update_area_t *update_area)
{
    if ( update_area )
    {
        // Update the virtual display.
        //
        einkfb_update_vfb_area(update_area);
        
        // Update the real display.
        //
        if ( hal_ops.hal_update_area && (EINKFB_SUCCESS == EINKFB_LOCK_ENTRY()) )
        {
            hal_ops.hal_update_area(update_area);
            EINKFB_LOCK_EXIT();
        }
    }
}

void einkfb_update_display(fx_type update_mode)
{
    // Update the virtual display.
    //
    einkfb_update_vfb(update_mode);

    // Update the real display.
    //
    if ( hal_ops.hal_update_display && (EINKFB_SUCCESS == EINKFB_LOCK_ENTRY()) )
    {
        hal_ops.hal_update_display(update_mode);
        EINKFB_LOCK_EXIT();
    }
}

void einkfb_restore_display(fx_type update_mode)
{
    // Switch the real display to the virtual one.
    //
    einkfb_set_fb_restore(true);
    
    // Update the real display with the virtual one's data.
    //
    einkfb_update_display(update_mode); 
    
    // Switch back to the real display.
    //
    einkfb_set_fb_restore(false);
}

void einkfb_clear_display(fx_type update_mode)
{
    struct einkfb_info info; einkfb_get_info(&info);
    einkfb_memset(info.start, EINKFB_WHITE, info.size);
    
    einkfb_update_display(update_mode);
}

EXPORT_SYMBOL(einkfb_lock_ready);
EXPORT_SYMBOL(einkfb_lock_release);
EXPORT_SYMBOL(einkfb_lock_entry);
EXPORT_SYMBOL(einkfb_lock_exit);
EXPORT_SYMBOL(einkfb_schedule_timeout);
EXPORT_SYMBOL(einkfb_blit);
EXPORT_SYMBOL(einkfb_stretch_nybble);
EXPORT_SYMBOL(einkfb_get_last_bounds_failure);
EXPORT_SYMBOL(einkfb_bounds_are_acceptable);
EXPORT_SYMBOL(einkfb_align_bounds);

