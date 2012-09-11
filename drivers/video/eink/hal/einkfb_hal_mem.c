/*
 *  linux/drivers/video/eink/hal/einkfb_hal_mem.c -- eInk frame buffer device HAL memory
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

// Hack Warning -- not true on all architectures.
#define VMALLOC_VMADDR(x) ((unsigned long)(x))

struct einkfb_vma_list_entry
{
	struct list_head list;
	struct vm_area_struct *vma;
};

static struct page **einkfb_pages = NULL;
static int num_einkfb_pages = 0;

static struct semaphore vma_list_semaphore;
static LIST_HEAD(vma_list);

#if PRAGMAS
    #pragma mark -
    #pragma mark Local Utilities
    #pragma mark -
#endif

static int add_vma(struct vm_area_struct *vma)
{
	struct einkfb_vma_list_entry *list_entry;

	list_entry = kmalloc(sizeof(struct einkfb_vma_list_entry), GFP_KERNEL);
	list_entry->vma = vma;

	down_interruptible(&vma_list_semaphore);

	list_add_tail((struct list_head *)list_entry, &vma_list);

	up(&vma_list_semaphore);

	return ( EINKFB_SUCCESS );
}

static struct einkfb_vma_list_entry *einkfb_find_vma_entry(struct vm_area_struct *vma)
{
	struct einkfb_vma_list_entry *entry = NULL;
	struct list_head *ptr = NULL;
	int done = 0;
  
	for ( ptr = vma_list.next; ptr != &vma_list && !done; ptr = ptr->next )
	{
		entry = list_entry(ptr, struct einkfb_vma_list_entry, list);
		if ( entry->vma == vma )
		    done = 1;
	}

    if ( !done )
        entry = NULL;

	return ( entry );
}

static int remove_vma(struct vm_area_struct *vma)
{
	struct einkfb_vma_list_entry *entry;

	down_interruptible(&vma_list_semaphore);

	entry = einkfb_find_vma_entry(vma);
	if ( entry )
	{
		list_del((struct list_head *)entry);
		kfree(entry);
	}

	up(&vma_list_semaphore);

	return ( EINKFB_SUCCESS );
}

static struct page *vmalloc_2_page(void *addr)
{
	unsigned long lpage;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	struct page *page;
  
	lpage = VMALLOC_VMADDR(addr);
	spin_lock(&init_mm.page_table_lock);

	pgd = pgd_offset(&init_mm, lpage);
	pmd = pmd_offset(pgd, lpage);
	pte = pte_offset_map(pmd, lpage);
	page = pte_page(*pte);

	spin_unlock(&init_mm.page_table_lock);

//  return ( vmalloc_to_page(addr) );
	return ( page );
}

static void einkfb_vma_open(struct vm_area_struct *vma)
{
	add_vma(vma);
}

static void einkfb_vma_close(struct vm_area_struct *vma)
{
	remove_vma(vma);
}

static struct page *einkfb_vma_nopage(struct vm_area_struct *vma, unsigned long address,
	int *type)
{
	unsigned long offset = address - vma->vm_start + (vma->vm_pgoff << PAGE_SHIFT);
	struct page *page = NOPAGE_SIGBUS;

	struct einkfb_info info;
	einkfb_get_info(&info);

	if ( offset < info.mem )
	{
        page = einkfb_pages[offset >> PAGE_SHIFT];
        get_page(page);
    }

	return ( page );
}

static int einkfb_alloc_page_array(size_t size, void *start)
{
	int result = EINKFB_FAILURE;

	num_einkfb_pages = (size >> PAGE_SHIFT) + 1;

	einkfb_pages = kmalloc(num_einkfb_pages * sizeof(struct page *), GFP_KERNEL);

    if ( einkfb_pages )
    {
        int i;
        
        for ( i = 0; (i << PAGE_SHIFT) < size; i++ )
            einkfb_pages[i] =  vmalloc_2_page(start + (i << PAGE_SHIFT));

        sema_init(&vma_list_semaphore, 1);

        result = EINKFB_SUCCESS;
    }

	return ( result );
}

static void einkfb_free_page_array(void)
{
    if ( einkfb_pages )
    {
        kfree(einkfb_pages);
        einkfb_pages = NULL;
    }
}

struct vm_operations_struct einkfb_vma_ops =
{
	open:   einkfb_vma_open,
	close:  einkfb_vma_close,
	nopage: einkfb_vma_nopage,
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
        dma_mmap_coherent(hal_info.fbinfo->device, vma, hal_info.start, hal_info.phys->addr,
            hal_info.phys->size);
    else
    {
        vma->vm_ops = &einkfb_vma_ops;
        vma->vm_flags |= VM_RESERVED;

        einkfb_vma_open(vma);
    }

	return ( EINKFB_SUCCESS );
}

void *einkfb_malloc(size_t size, einkfb_dma_addr_t *phys, bool mmap)
{
    void *result = NULL;
    
    if ( size )
    {
        if ( phys )
        {
            struct einkfb_info info;
            einkfb_get_info(&info);

	        result = dma_alloc_coherent(info.fbinfo->device, size, &phys->addr,
	            GFP_DMA);
	            
	        if ( result )
	            phys->size = size;
        }
        else
        {
            result = vmalloc(size);
            
            if ( result && mmap )
            {
                if ( EINKFB_FAILURE == einkfb_alloc_page_array(size, result) )
                {
                    vfree(result);
                    result = NULL;
                }
            }
        }
    }
    
    return ( result );
}

void einkfb_free(void *ptr, einkfb_dma_addr_t *phys, bool mmap)
{
    if ( ptr )
    {
        if ( mmap )
            einkfb_free_page_array();

        if ( phys )
        {
            struct einkfb_info info;
            einkfb_get_info(&info);
	        
	        dma_free_coherent(info.fbinfo->device, phys->size, ptr, phys->addr);
	    }
        else
            vfree(ptr);
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
