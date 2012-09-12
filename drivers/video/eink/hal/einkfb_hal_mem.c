/*
 *  linux/drivers/video/eink/hal/einkfb_hal_mem.c -- eInk frame buffer device HAL memory
 *
 *      Copyright (C) 2005-2009 Amazon Technologies
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include "einkfb_hal.h"

#if PRAGMAS
    #pragma mark Local Utilities
    #pragma mark -
#endif

static void einkfb_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	// At the moment, we're just using the deferred I/O mechanism to map in
	// pages for us.  We're not using it to update the display with since
	// we have our own, more direct method for doing that.
}

static struct fb_deferred_io einkfb_defio =
{
    .delay          = HZ,
    .deferred_io    = einkfb_deferred_io,
};

#if PRAGMAS
    #pragma mark -
    #pragma mark External Interfaces
    #pragma mark -
#endif

int einkfb_mmap(FB_MMAP_PARAMS)
{
    struct einkfb_info hal_info;
    einkfb_get_info(&hal_info);
    
    if ( hal_info.dma )
        dma_mmap_writecombine(hal_info.fbinfo->device, vma, hal_info.start, hal_info.phys->addr,
            hal_info.phys->size);

    return ( EINKFB_SUCCESS );
}

void *einkfb_malloc(size_t size, einkfb_dma_addr_t *phys, bool mmap)
{
    void *result = NULL;
    
    if ( size )
    {
        struct einkfb_info info;
        einkfb_get_info(&info);
        
        if ( phys )
        {
            result = dma_alloc_writecombine(info.fbinfo->device, size, &phys->addr,
                GFP_DMA);
                
            if ( result )
            {
                // Use the DMA mmap() handler for our DMA page mapping. 
                //
                if ( mmap )
                    info.fbinfo->fbops->fb_mmap = einkfb_mmap;
                 
                phys->size = size;
            }
        }
        else
        {
            result = vmalloc(size);

            // Use the deferred I/O mechanism to handle our virtual page mapping.
            //
            if ( mmap && result )
            {
                info.fbinfo->fbdefio = &einkfb_defio;
                fb_deferred_io_init(info.fbinfo);
            }
        }
    }
    
    return ( result );
}

void einkfb_free(void *ptr, einkfb_dma_addr_t *phys, bool mmap)
{
    if ( ptr )
    {
        struct einkfb_info info;
        einkfb_get_info(&info);
        
        if ( phys )
            dma_free_writecombine(info.fbinfo->device, phys->size, ptr, phys->addr);
        else
        {
            if ( mmap )
            {
                fb_deferred_io_cleanup(info.fbinfo);
                info.fbinfo->fbdefio = NULL;
            }
            
            vfree(ptr);
        }
    }
}

static void einkfb_memcpy_schedule(u8 *dst, const u8 *src, size_t dst_length, size_t src_length)
{
    int  set_val = 0, i = 0, length = EINKFB_MEMCPY_MIN, num_loops = dst_length/length,
         remainder = dst_length % length;
    bool set = 0 == src_length, done = false;
    
    if ( 0 != num_loops )
        einkfb_debug("num_loops @ %d bytes = %d, remainder = %d\n",
            length, num_loops, remainder);

    if ( set )
        set_val = *((int *)src);
    
    // Set or copy EINKFB_MEMCPY_MIN bytes at a time.  While there are still
    // bytes to set or copy, yield the CPU.
    //
    do
    {
        if ( 0 >= num_loops )
            length = remainder;
        
        if ( set )
            memset(&dst[i], set_val, length);
        else
            memcpy(&dst[i], &src[i], length);
          
        i += length;
        
        if ( i < dst_length )
        {
            EINKFB_SCHEDULE();
            num_loops--;
        }
        else
            done = true;
    }
    while ( !done );
}

int einkfb_memcpy(bool direction, unsigned long flag, void *destination, const void *source, size_t length)
{
    int result = EINKFB_IOCTL_FAILURE;
    
    if ( destination && source && length )
    {
        result = EINKFB_SUCCESS;
        
        // Do a memcpy() in kernel space; otherwise, copy to/from user-space,
        // as specified.
        //
        if ( EINKFB_IOCTL_KERN == flag )
            einkfb_memcpy_schedule((u8 *)destination, (u8 *)source, length, length);
        else
        {
            if ( EINKFB_IOCTL_FROM_USER == direction )
                result = copy_from_user(destination, source, length);
            else
                result = copy_to_user(destination, source, length);
        }
    }
    
    return ( result );
}

void einkfb_memset(void *destination, int value, size_t length)
{
    if ( destination && length )
        einkfb_memcpy_schedule((u8 *)destination, (u8 *)&value, length, 0);
}

EXPORT_SYMBOL(einkfb_malloc);
EXPORT_SYMBOL(einkfb_free);
EXPORT_SYMBOL(einkfb_memcpy);
EXPORT_SYMBOL(einkfb_memset);
