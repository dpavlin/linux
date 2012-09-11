/*
 *  linux/drivers/video/eink/hal/einkfb_hal_io.c -- eInk frame buffer device HAL I/O
 *
 *      Copyright (C) 2005-2007 Lab126
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include "einkfb_hal.h"

#if PRAGMAS
    #pragma mark Definitions/Globals
#endif

static einkfb_ioctl_hook_t einkfb_ioctl_hook = NULL;
static update_area_t einkfb_update_area;

#if PRAGMAS
    #pragma mark -
    #pragma mark Local Utilities
    #pragma mark -
#endif

struct load_buffer_t
{
    u8 *src, *dst;
};
typedef struct load_buffer_t load_buffer_t;

static void einkfb_load_buffer(int x, int y, int rowbytes, int bytes, void *data)
{
    load_buffer_t *load_buffer = (load_buffer_t *)data;
    load_buffer->dst[bytes] = load_buffer->src[(rowbytes * y) + x];
}

static int einkfb_validate_area_data(update_area_t *update_area)
{
    int xstart = update_area->x1, xend = update_area->x2,
        ystart = update_area->y1, yend = update_area->y2,
        
        xres = xend - xstart,
        yres = yend - ystart,
        
        buffer_size = 0;
    
    struct einkfb_info info;
    einkfb_get_info(&info);
    
    if ( einkfb_bounds_are_acceptable(xstart, xres, ystart, yres) )
        buffer_size = BPP_SIZE((xres * yres), info.bpp);

    // Fix the bounds to the appropriate byte alignment if the passed-in bounds
    // aren't byte aligned and we're just doing an area update from the
    // framebuffer itself.
    //
    if ( (0 == buffer_size) && NOT_BYTE_ALIGNED() && (NULL == update_area->buffer) )
        if ( einkfb_align_bounds((rect_t *)update_area) )
            buffer_size = BPP_SIZE((xres * yres), info.bpp);

    return ( buffer_size );
}

static update_area_t *einkfb_set_area_data(unsigned long flag, update_area_t *update_area)
{
    update_area_t *result = NULL;
    
    if ( update_area )
    {
        result = &einkfb_update_area;
        
        // If debugging is enabled, output the passed-in update_area data.
        //
        einkfb_debug("x1       = %d\n",      update_area->x1);
        einkfb_debug("y1       = %d\n",      update_area->y1);
        einkfb_debug("x2       = %d\n",      update_area->x2);
        einkfb_debug("y2       = %d\n",      update_area->y2);
        einkfb_debug("which_fx = %d\n",      update_area->which_fx);
        einkfb_debug("buffer   = 0x%08lX\n", (unsigned long)update_area->buffer);
        
        if ( result )
        {
            int success = einkfb_memcpy(EINKFB_IOCTL_FROM_USER, flag, result, update_area, sizeof(update_area_t));
            unsigned char *buffer = NULL;

            if ( EINKFB_SUCCESS == success )
            {
                int buffer_size = einkfb_validate_area_data(result);
                
                success = EINKFB_FAILURE;
                result->buffer = NULL;

                if ( buffer_size )
                {
                    struct einkfb_info info;
                    einkfb_get_info(&info);
                    
                    buffer = info.buf;
    
                    if ( update_area->buffer )
                        success = einkfb_memcpy(EINKFB_IOCTL_FROM_USER, flag, buffer, update_area->buffer, buffer_size);
                    else
                    {
                        load_buffer_t load_buffer;

                        load_buffer.src = info.start;
                        load_buffer.dst = buffer;
                        
                        einkfb_blit(result->x1, result->x2, result->y1, result->y2, einkfb_load_buffer, (void *)&load_buffer);
                        success = EINKFB_SUCCESS;
                    }
                }
            }

            if ( EINKFB_SUCCESS == success )
                result->buffer = buffer;
            else
                result = NULL;
        }
    }
    
    return ( result );
}

static char unknown_cmd_string[32];

char *einkfb_get_cmd_string(unsigned int cmd)
{
    char *cmd_string = NULL;
    
    switch ( cmd )
    {
        // Supported by HAL.
        //
        case FBIO_EINK_UPDATE_DISPLAY:
            cmd_string = "update_display";
        break;
        
        case FBIO_EINK_UPDATE_DISPLAY_AREA:
            cmd_string = "update_display_area";
        break;
        
        case FBIO_EINK_RESTORE_DISPLAY:
            cmd_string = "restore_display";
        break;
        
        case FBIO_EINK_SET_REBOOT_BEHAVIOR:
            cmd_string = "set_reboot_behavior";
        break;

        case FBIO_EINK_GET_REBOOT_BEHAVIOR:
            cmd_string = "get_reboot_behavior";
        break;

        // Supported by Shim.
        //
        case FBIO_EINK_UPDATE_DISPLAY_FX:
            cmd_string = "update_dislay_fx";
        break;
        
        case FBIO_EINK_SPLASH_SCREEN:
            cmd_string = "splash_screen";
        break;
        
        case FBIO_EINK_SPLASH_SCREEN_SLEEP:
            cmd_string = "splash_screen_sleep";
        break;
        
        case FBIO_EINK_OFF_CLEAR_SCREEN:
            cmd_string = "off_clear_screen";
        break;
        
        case FBIO_EINK_CLEAR_SCREEN:
            cmd_string = "clear_screen";
        break;
        
        case FBIO_EINK_POWER_OVERRIDE:
            cmd_string = "power_override";
        break;
        
        case FBIO_EINK_FAKE_PNLCD:
            cmd_string = "fake_pnlcd";
        break;
        
        case FBIO_EINK_PROGRESSBAR:
            cmd_string = "progressbar";
        break;
        
        case FBIO_EINK_PROGRESSBAR_SET_XY:
            cmd_string = "progressbar_set_xy";
        break;
        
        case FBIO_EINK_PROGRESSBAR_BADGE:
            cmd_string = "progressbar_badge";
        break;
        
        // Unknown and/or unsupported.
        //
        default:
            sprintf(unknown_cmd_string, "unknown cmd = 0x%08X", cmd);
            cmd_string = unknown_cmd_string;
        break;
    }
    
    return ( cmd_string );
}

#if PRAGMAS
    #pragma mark -
    #pragma mark External Interfaces
    #pragma mark -
#endif

int einkfb_ioctl_dispatch(unsigned long flag, struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    bool done = !EINKFB_IOCTL_DONE, bad_arg = false;
    unsigned long start_time = jiffies, stop_time;
    int result = EINKFB_SUCCESS;

    unsigned long local_arg = arg;
    unsigned int local_cmd = cmd;
    
    einkfb_debug_ioctl("%s(0x%08lX), start = %lu\n", einkfb_get_cmd_string(cmd),
        arg, start_time);

    // If there's a hook, give it the pre-command call.
    //
    if ( einkfb_ioctl_hook )
        done = (*einkfb_ioctl_hook)(einkfb_ioctl_hook_pre, flag, &local_cmd, &local_arg);
    
    // Process the command if it hasn't already been handled.
    //
    if ( !done )
    {
        update_area_t *update_area = NULL;
        
        switch ( local_cmd )
        {
            case FBIO_EINK_UPDATE_DISPLAY:
                 einkfb_update_display(UPDATE_MODE(local_arg));
            break;
            
            case FBIO_EINK_UPDATE_DISPLAY_AREA:
                update_area = einkfb_set_area_data(flag, (update_area_t *)local_arg);
                
                if ( update_area )
                {
                    fx_type saved_fx = update_area->which_fx;
                    
                    // If there's a hook, give it the beginning in-situ call.
                    //
                    if ( einkfb_ioctl_hook )
                        (*einkfb_ioctl_hook)(einkfb_ioctl_hook_insitu_begin, flag, &local_cmd,
                            (unsigned long *)&update_area);

                    // Update the display with the area data, preserving and
                    // normalizing which_fx as we go.
                    //
                    update_area->which_fx = UPDATE_AREA_MODE(saved_fx);
                    einkfb_update_display_area(update_area);
                    update_area->which_fx = saved_fx;
                    
                    // If there's a hook, give it the ending in-situ call.
                    //
                    if ( einkfb_ioctl_hook )
                        (*einkfb_ioctl_hook)(einkfb_ioctl_hook_insitu_end, flag, &local_cmd,
                            (unsigned long *)&update_area);
                }
                else
                    goto failure;
            break;
            
            case FBIO_EINK_RESTORE_DISPLAY:
                einkfb_restore_display(UPDATE_MODE(local_arg));
            break;
            
            case FBIO_EINK_SET_REBOOT_BEHAVIOR:
                einkfb_set_reboot_behavior((reboot_behavior_t)local_arg);
            break;
            
            case FBIO_EINK_GET_REBOOT_BEHAVIOR:
                if ( local_arg )
                {
                    reboot_behavior_t reboot_behavior = einkfb_get_reboot_behavior();

                    einkfb_memcpy(EINKFB_IOCTL_TO_USER, flag, (reboot_behavior_t *)local_arg,
                        &reboot_behavior, sizeof(reboot_behavior_t));
                }
                else
                    goto failure;
            break;
            
            failure:
                bad_arg = true;
            default:
                result = EINKFB_IOCTL_FAILURE;
            break;
        }
    }

    // If there's a hook and we haven't determined that we've received a bad argument, give it
    // the post-command call.  Use the originally passed-in cmd & arg here instead of local copies
    // in case they were changed in the pre-command processing.
    //
    if ( !bad_arg && einkfb_ioctl_hook )
    {
        done = (*einkfb_ioctl_hook)(einkfb_ioctl_hook_post, flag, &cmd, &arg);
        
        // If the hook processed the command, don't pass along the HAL's failure to do so.
        //
        if ( done && (EINKFB_IOCTL_FAILURE == result) )
            result = EINKFB_SUCCESS;
    }
    
    stop_time = jiffies;
    
    einkfb_debug_ioctl("result = %d, stop = %lu, elapsed = %ums\n", result, stop_time, 
        jiffies_to_msecs(stop_time - start_time));

    return ( result );
}

int einkfb_ioctl(FB_IOCTL_PARAMS)
{
    return ( einkfb_ioctl_dispatch(EINKFB_IOCTL_USER, info, cmd, arg) );
}

void einkfb_set_ioctl_hook(einkfb_ioctl_hook_t ioctl_hook)
{
    // Need to make a queue of these if this becomes truly useful.
    //
    if ( ioctl_hook )
        einkfb_ioctl_hook = ioctl_hook;
    else
        einkfb_ioctl_hook = NULL;
}

EXPORT_SYMBOL(einkfb_get_cmd_string);
EXPORT_SYMBOL(einkfb_ioctl_dispatch);
EXPORT_SYMBOL(einkfb_ioctl);
EXPORT_SYMBOL(einkfb_set_ioctl_hook);
