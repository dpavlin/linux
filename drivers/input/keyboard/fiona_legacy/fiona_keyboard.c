/******************************************************************
 *
 *  File:   fiona_keyboard.c
 *
 *  Author: Nick Vaccaro <nvaccaro@lab126.com>
 *
 *  Date:   12/07/05
 *
 * Copyright 2005, Lab126, Inc.  All rights reserved.
 *
 *  Description:
 *      Fake Fiona keyboard driver. This provides /dev/misc/input and
 *      an API to push events into it.
 *
 ******************************************************************/

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/kbd_kern.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <asm/delay.h>
#include <asm/io.h>
#include "fiona.h"
#include "fpow.h"
#include "fiona_sniffer.h"
#include "fiona_eventmap.h"
#ifdef CONFIG_FIONA_PM_KEYBOARD
#include "keyboard_pm.h"
#endif

#include <asm/arch/boot_globals.h>
#include "fiona_keymap.h"
#include "fiona_keymap.c"

// Local Compiler Flags to enable/disable debugging
#undef KBD_DEBUG_FLOW
#ifdef KBD_DEBUG_FLOW
  #define flowLog(x...)  printk("KBD FLOW: " x)
#else
  #define flowLog(x...)  do {} while(0)
#endif

#undef KBD_DEBUG_BATTERY_SIGNAL

#undef HACK_EXTENDED_LOG
#undef HACK_EXTENDED_DEBUG_QUEUE_REMOVE_LOG
#ifdef HACK_EXTENDED_LOG
    #define hextlog(x...)  printk(x)
    #ifdef HACK_EXTENDED_DEBUG_QUEUE_REMOVE_LOG
        #define hextextlog(x...)  printk(x)
    #else
        #define hextextlog(x...)    do {} while(0)
    #endif
#else
    #define hextlog(x...)       do {} while(0)
    #define hextextlog(x...)    do {} while(0)
#endif

// Make up some constants
#define FIONA_KEYBOARD_DEVICE_NAME      "Fiona Keyboard Device"
#define FIONA_KEYBOARD_DEV_ENTRY_NAME   "keyboard"
#define FIONA_KEYBOARD_DRIVER_VERSION   "v1.0"

//
// Local Prototypes
//
static int fiona_kbd_open(struct inode *node, struct file *filep);
static int fiona_kbd_close(struct inode *node, struct file *filep);
static ssize_t fiona_kbd_read(struct file * file, char * buffer, size_t count, loff_t *ppos);
static ssize_t fiona_kbd_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos);
static unsigned int fiona_kbd_poll(struct file *file, poll_table * wait);
static int fasync_kbd(int fd, struct file *filp, int on);

int     __find_event_in_queue(FPOW_EVENT event);
void    print_queue(char *str, int forcePrint);

void setEventQueueStalled(int val);

//
// External Prototypes
//

//
// External Globals
//

//
// Globals
//
int         g_fiona_kbd_used = 0;
static int  g_kbd_data_queued = 0;
static int  g_last_event_passed_to_java = INVALID_EVENT;

#ifdef HACK_EXTENDED_LOG
int g_fiona_ext_sniff_debug = 1;
#else
int g_fiona_ext_sniff_debug = 0;
#endif

struct  input_dev g_fiona_kbd_dev;

struct file_operations keyboard_fops = {
    read:       fiona_kbd_read,
    write:      fiona_kbd_write,
    poll:       fiona_kbd_poll,
    open:       fiona_kbd_open,
    release:    fiona_kbd_close,
#ifdef LOCAL_CONFIG_PM
    suspend:    fiona_keyboard_suspend
    resume:     fiona_keyboard_resume   
#endif
    fasync:     fasync_kbd,
};

/*
 * Initialize driver.
 */
static struct miscdevice keyboard_device = {
    IOC_KEYBOARD_MINOR, FIONA_KEYBOARD_DEV_ENTRY_NAME, &keyboard_fops 
};

int get_last_passed_event(void)
{
    return(g_last_event_passed_to_java);
}

/*****************************************************************************
 * Queue Support
 *****************************************************************************/
int     qSizeWrap = (AUX_BUF_SIZE-1);
struct  kbd_queue *queue;    /* Keyboard data buffer. */
spinlock_t g_kbd_lock = SPIN_LOCK_UNLOCKED;


int eventHandlerActive(void) {
    return(g_fiona_kbd_used);
}

static int
fasync_kbd(int fd, struct file *filp, int on)
{
    int retval;

    retval = fasync_helper(fd, filp, on, &queue->fasync);
    if( retval < 0)
        return retval;
    return 0;
}

void kbd_clear_queue(void)
{
    unsigned long flags;
    spin_lock_irqsave(&g_kbd_lock, flags);
    queue->head = queue->tail = 0;
    spin_unlock_irqrestore(&g_kbd_lock, flags);
}

static void kbd_clear_init_queue(void)
{
    memset(queue, 0, sizeof(*queue));
    queue->head = queue->tail = 0;
    setEventQueueStalled(0);
    init_waitqueue_head(&queue->proc_list);
}

void fiona_clear_unstall_event_queue(void)
{
    kbd_clear_queue();
    setEventQueueStalled(0);
}


/*
 *  Check if queue is empty
 */
static inline int queue_empty(void)
{
    return queue->head == queue->tail;
}

int fiona_event_queue_empty(void) {
    return(queue_empty());
}

/*****************************************************************************
 * Queue HACK Support START
 *****************************************************************************/

static int g_event_queue_stalled = 0;

void __wake_kdb_queue_if_needed(void)
{
    if (0 == g_event_queue_stalled)  {
        kill_fasync(&queue->fasync, SIGIO, POLL_IN);
        wake_up_interruptible(&queue->proc_list);
    }
    else
        flowLog("__wake_kdb_queue_if_needed() found Q stalled, NOT passing data up..\n");
}

void wake_kdb_queue_if_needed(void)
{
    if (0 == queue_empty()) {
        flowLog("wake_kdb_queue_if_needed() found queue NOT empty, passing data up..\n");
        __wake_kdb_queue_if_needed();
    }
}

int isEventQueueStalled(void) 
{
    return(g_event_queue_stalled);
}

void setEventQueueStalled(int val) 
{
    if (val) {
        print_queue("...QUEUE STALLED...\n",0);
    }
    else {
        print_queue("...QUEUE UNSTALLED...\n",0);
    }
    g_event_queue_stalled = val;
}

void unstallEventQueue(void)
{
    setEventQueueStalled(0);
    wake_kdb_queue_if_needed();
}




/*********************************************************
 * Routine:     put_at_front_of_queue
 * Purpose:     Add an event to the front of the Fiona 
 *              keyboard / event queue.
 * Paramters:   element - element to insert at FRONT of queue.
 * Returns:     nothing
 * Assumptions: Elements are added to the head of the queue,
 *              removed from the tail...
 *********************************************************/
void put_at_front_of_queue(int element)
{
    unsigned long flags;
    int tail;
    int scancode = (element & 0x000000FF);
    int released = (scancode & ~0x0000007F);
    int the_key = 0;

    if (scancode >= FIONA_KEYMAP_SIZE) {
        // It's a user event, translate event to an event-tied keycode
        if ((scancode >= FIONA_EVENT_FIRST) && (scancode <= FIONA_EVENT_LAST)) {
            the_key = get_fiona_event_code(scancode);     // Event is valid
            if (released)
                the_key |= KEY_WAS_RELEASED;
        }
        else {
            printk("Fiona event scancode=%d is out-of-range !!\n",scancode);
            return;
        }

		hextlog("Queueing EVENT front of Q: %s (scancode = 0x%x, the_key = 0x%x)\n",fiona_event_keycode_to_str(scancode|(released?0x80:0)),scancode,the_key);
    }
    else {
        printk("Error - wrong data passed to put_at_front_of_queue()\n");
        return;
    }

    if (g_fiona_kbd_used) {
        spin_lock_irqsave(&g_kbd_lock, flags);
        tail = (queue->tail)?(queue->tail-1):(AUX_BUF_SIZE-1);
        if (queue->head == tail) {
            // Queue is too full, we need to lose an event off the end...
            queue->head = (queue->head)?(queue->head-1):(AUX_BUF_SIZE-1);
            hextlog("put_at_front_of_queue() FULL, set head = %d\n",(int)queue->head);
        }
        queue->buf[tail] = the_key;
        queue->tail = tail;
        hextlog("put_at_front_of_queue() set tail = %d\n",(int)queue->tail);
        print_queue(NULL,0);

        hextlog("put_at_front_of_queue() calling __wake_kdb_queue_if_needed()\n");
        __wake_kdb_queue_if_needed();
        spin_unlock_irqrestore(&g_kbd_lock, flags);
    }
}

/*********************************************************
 * Routine:     __fiona_kdb_queue_byte
 * Purpose:     Add an event to the Fiona keyboard / event
 *              queue.
 * Paramters:   val - value of element to insert into queue.
 * Returns:     nothing
 * Assumptions: g_kbd_lock is held (queue is protected)
 *              Elements are added to the head of the queue,
 *              removed from the tail...
 *********************************************************/
void __fiona_kdb_queue_byte(int val)
{
    if (g_fiona_kbd_used) {
        int head = queue->head;

        queue->buf[head] = val;
        head = (head + 1) & (AUX_BUF_SIZE-1);
        if (head != queue->tail) {
            queue->head = head;
            __wake_kdb_queue_if_needed();
        }
    }
}

void set_data_queued_flag(void) {
    g_kbd_data_queued = 1;
}

void clear_data_queued_flag(void) {
    g_kbd_data_queued = 0;
}

int has_new_data_arrived(void) {
    return(g_kbd_data_queued);
}

/*********************************************************
 * Routine:     kdb_queue_byte
 * Purpose:     Add an event to the Fiona keyboard / event
 *              queue.
 * Paramters:   val - value of element to insert into queue.
 * Returns:     nothing
 * Assumptions: none
 *********************************************************/
static void
kdb_queue_byte(int val)
{
    unsigned long flags;
    flowLog("kdb_queue_byte(0x%x)\n",val);
    spin_lock_irqsave(&g_kbd_lock, flags);
    __fiona_kdb_queue_byte(val);
    set_data_queued_flag();
    print_queue(NULL,0);
    spin_unlock_irqrestore(&g_kbd_lock, flags);
}

static char *
fpow_passed_event_to_str(int event)
{
    switch(event)  {
        case EVENT_BATTERY_NO_WAN:
            return("BATTERY_NO_WAN");
        case EVENT_BATTERY_LOW:
            return("BATTERY_LOW");
        case EVENT_BATTERY_CRITICAL:
            return("BATTERY_CRITICAL");
        case EVENT_WAN_SWITCH_ON:
            return("WAN_SWITCH_ON");
        case EVENT_WAN_SWITCH_OFF:
            return("WAN_SWITCH_OFF");
        case EVENT_AC_INSERTED:
            return("AC_INSERTED");
        case EVENT_AC_REMOVED:
            return("AC_REMOVED");
        case EVENT_USB_PLUG_INSERTED:
            return("USB_PLUG_INSERTED");
        case EVENT_USB_PLUG_REMOVED:
            return("USB_PLUG_REMOVED");
        case EVENT_HEADPHONES_INSERTED:
            return("HEADPHONES_INSERTED");
        case EVENT_HEADPHONES_REMOVED:
            return("HEADPHONES_REMOVED");
        case EVENT_WAN_RING:
            return("WAN_RING");
        case EVENT_KEY_WAKE:
            return("KEY_WAKE");
        case EVENT_AUDIO_ON:
            return("AUDIO_ON");
        case EVENT_AUDIO_OFF:
            return("AUDIO_OFF");
        case EVENT_POWER_SWITCH_ON:
            return("POWER_SWITCH_ON");
        case EVENT_POWER_SWITCH_OFF:
            return("POWER_SWITCH_OFF");
        case EVENT_MMC:
            return("MMC");
        case EVENT_VALID_PASSWORD:
            return("VALID_PASSWORD");
        case EVENT_INVALID_PASSWORD:
            return("INVALID_PASSWORD");
        case EVENT_TIMEOUT_1:
            return("T1");
        case EVENT_TIMEOUT_2:
            return("T2");
        case EVENT_TIMEOUT_3:
            return("T3");
        case EVENT_TIMEOUT_USB_WAKE:
            return("TUSB");
        case EVENT_WATERMARK_CHECK:
            return("VC");
        case EVENT_PRIME_PUMP:
            return("PRIME");
        case EVENT_TPH_COMPLETE:
            return("TPH_COMP");
        case EVENT_USB_WAKE:
            return("USB_WAKE");
        case EVENT_WAN_SWITCH_SWEEPER:
            return("WAN_SWEEP");
        case EVENT_TWO_FINGER_SWEEPER:
            return("AF_SWEEP");
        default:
            return("KBS");
    }
}

int preScanQueueForward(int start,int event, int *pos) {
    int eventFound = 0;
    int cur = start;

    // Working from the index_to_remove backwards until we hit the queue element
    // having the proper index, shift all queue elements
    // behind the one we're removing forward one position.
    do {
        if (queue->buf[cur] == event) {
            hextlog("preScan: found event %s at position %d\n",fpow_passed_event_to_str(event),cur);
            *pos = cur;
            eventFound = 1;
            goto exit;
        }

        if (cur == queue->head)
            goto exit;

        // Point src forward to next event for next loop
        cur = ((cur + 1) & (AUX_BUF_SIZE-1));
    }
    while (1);

    hextlog("preScanQueueForward: Event found = %d, pos = %d\n",eventFound,(eventFound?*pos:-1));
exit:
    return(eventFound);
}


/*********************************************************
 * Routine:     __remove_event_index_from_queue
 *
 * Purpose:     Remove element whose index is the input 
 *              parameter "index" from the event queue
 *
 * Paramters:   index_to_remove - index of element to remove
 *                      from the event queue
 *
 *              Elements are shifted down (head decrements on delete)
 *                  
 * Returns:     none
 *
 * Assumptions:
 *  1) Element having that index actually exists.
 *  2) Queue is protected (caller has g_kbd_lock held)
 *  3) Elements are added to head and removed from tail
 *********************************************************/
void __remove_event_index_from_queue(int index_to_remove)
{
    int dst;
    int src;

    hextextlog("QUEUE_REMOVE(%d): ENTER h=%d,t=%d\n",index_to_remove,(int)queue->head,(int)queue->tail);

    // Working from the index_to_remove backwards until we hit the queue element
    // having the proper index, shift all queue elements
    // in front of the one we're removing backwards one position, then move head back one.
    dst = index_to_remove;
    src = ((dst + 1) & (AUX_BUF_SIZE-1));

    do {
        hextextlog("QUEUE_REMOVE: buf[%d]=0x%x --> buf[%d]=0x%x\n",src,queue->buf[src],dst,queue->buf[dst]);
        // Move the event forward in the queue
        queue->buf[dst] = queue->buf[src];

        // If we've walked the queue, break
        if (src == queue->head) {
            hextextlog("QUEUE_REMOVE: Reached head at %d\n",src);
            break;
        }

        // Point src forward to next event for next loop
        src = ((src + 1) & (AUX_BUF_SIZE-1));

        // Adjust dst pointer to be 1 behind source pointer
        dst = ((dst + 1) & (AUX_BUF_SIZE-1));

    }
    while (src <= queue->head);

    // Now adjust tail, it should point to dst
    queue->head = dst;
    hextextlog("QUEUE_REMOVE: EXIT   h=%d,t=%d\n",(int)queue->head,(int)queue->tail);
}

void print_queue(char *str, int forcePrint)
{
    int cur = queue->tail;
    int first = 1;
    int count = 0;

    if ((forcePrint == 0))
        return;

    // Print header string if supplied
    if (str != NULL) {
        hextlog(str);
    }

    if (queue_empty())  {
        hextlog("Q(%d,%d): EMPTY\n",(int)queue->tail, (int)queue->head);
        goto exit;
    }

    while (cur != queue->head)  {
#if 0
        if (count && ((count % 10) == 0))
            printk("\nHLOG: ");
#endif
        if (first)  {
            first = 0;
            count++;
            hextlog("Q(%d,%d): %s",(int)queue->tail,(int)queue->head,fpow_passed_event_to_str(queue->buf[cur]));
        }
        else  {
            printk(" : %s",fpow_passed_event_to_str(queue->buf[cur]));
            count++;
        }
        cur = (cur + 1) & (AUX_BUF_SIZE-1);
    }
    hextlog("\n");

exit:
    return;
}


/***************************************************************************
 * Routine:     convertEventToQueueEvent
 * Purpose:     Passed an FPOW event, this routine will convert it into the
 *              matching event type that would be found in the event queue
 *              and return that converted type.
 * Paramters:   event - the FPOW_EVENT to convert to a queue event type
 * Returns:     int - event type as would be found in the event queue
 * Assumptions: none
 ***************************************************************************/
static int
convertEventToQueueEvent(int event)
{
    int qEvent = INVALID_EVENT;
    switch(event)  {
        case POWER_SWITCH_ON_EVENT:
            qEvent = EVENT_POWER_SWITCH_ON;
            break;
        case POWER_SWITCH_OFF_EVENT:
            qEvent = EVENT_POWER_SWITCH_OFF;
            break;
        case WAN_SWITCH_ON_EVENT:
            qEvent = EVENT_WAN_SWITCH_ON;
            break;
        case WAN_SWITCH_OFF_EVENT:
            qEvent = EVENT_WAN_SWITCH_OFF;
            break;
        case MMC_EVENT:
            qEvent = EVENT_MMC;
            break;
        case AC_PLUG_INSERTED_EVENT:
            qEvent = EVENT_AC_INSERTED;
            break;
        case AC_PLUG_REMOVED_EVENT:
            qEvent = EVENT_AC_REMOVED;
            break;
        case USB_PLUG_INSERTED_EVENT:
            qEvent = EVENT_USB_PLUG_INSERTED;
            break;
        case USB_PLUG_REMOVED_EVENT:
            qEvent = EVENT_USB_PLUG_REMOVED;
            break;
        case HEADPHONES_INSERTED_EVENT:
            qEvent = EVENT_HEADPHONES_INSERTED;
            break;
        case HEADPHONES_REMOVED_EVENT:
            qEvent = EVENT_HEADPHONES_REMOVED;
            break;
        case TIMEOUT_EVENT:
            qEvent = EVENT_TIMEOUT_1;
            break;
        case LOW_BATTERY_EVENT:
            qEvent = EVENT_BATTERY_LOW;
            break;
        case CRITICAL_BATTERY_EVENT:
            qEvent = EVENT_BATTERY_CRITICAL;
            break;
        case WAN_RING_EVENT:
            qEvent = EVENT_WAN_RING;
            break;
        case AUDIO_ON_EVENT:
            qEvent = EVENT_AUDIO_ON;
            break;
        case AUDIO_OFF_EVENT:
            qEvent = EVENT_AUDIO_OFF;
            break;
        case TIMEOUT2_EVENT:
            qEvent = EVENT_TIMEOUT_2;
            break;
        case VALID_PASSWORD_EVENT:
            qEvent = EVENT_VALID_PASSWORD;
            break;
        case PRIME_EVENT:
            qEvent = EVENT_PRIME_PUMP;
            break;
        case NO_WAN_BATTERY_EVENT:
            qEvent = EVENT_BATTERY_NO_WAN;
            break;
        case INVALID_PASSWORD_EVENT:
            qEvent = EVENT_INVALID_PASSWORD;
            break;
        case WATERMARK_CHECK_EVENT:
            qEvent = EVENT_WATERMARK_CHECK;
            break;
        case TPH_COMPLETE_EVENT:
            qEvent = EVENT_TPH_COMPLETE;
            break;
        case USB_WAKE_EVENT:
            qEvent = EVENT_USB_WAKE;
            break;
        case TIMEOUT_USB_WAKE_EVENT:
            qEvent = EVENT_TIMEOUT_USB_WAKE;
            break;
        case TIMEOUT3_EVENT:
            qEvent = EVENT_TIMEOUT_3;
            break;
        case WAN_SWITCH_SWEEPER_EVENT:
            qEvent = EVENT_WAN_SWITCH_SWEEPER;
            break;
        case TWO_FINGER_SWEEPER_EVENT:
            qEvent = EVENT_TWO_FINGER_SWEEPER;
            break;

        // This is different than an actual ALT+FONT key combo in the queue
        // (they come as separate key presses/releases)
        case KEYLOCK_EVENT:
            qEvent = EVENT_KEY_WAKE;
            break;

        // This event isn't actually defined for the event pipeline.   It's handled
        // completely within PowerManager.java and would never be found in the
        // event queue
        case VIDEO_UPDATED_EVENT:

        // The following events would never come (they're HW events, not SW recognizable)
        case OUT_OF_POWER_EVENT:
        
        // The following cases aren't system events, but come
        // across as key events instead....
        case BUTTON_PRESS_EVENT:
        case SCROLLWHEEL_EVENT:
        case KEY_PRESS_EVENT:
        default:
            qEvent = INVALID_EVENT;
            break;
    }
    return(qEvent);
}


int __find_event_in_queue(FPOW_EVENT event) {
    int cur = queue->tail;
    int found = 0;
    int done = 0;
    int queueEvent = INVALID_EVENT;

    queueEvent = convertEventToQueueEvent(event);
    if (queueEvent == INVALID_EVENT)
        return(0);  // No invalid event in queue

    do {
        if (queue->buf[cur] == queueEvent)  {
            found = 1;
            done = 1;
            break;
        }
        if (cur == queue->head)
            done = 1;
        else
            cur = (cur + 1) & (AUX_BUF_SIZE-1);
    } while (0 == done);

    if (found)
        hextlog("__find_event_in_queue(%s) FOUND IT\n",fpow_passed_event_to_str(queueEvent));
    else
        hextlog("__find_event_in_queue(%s) NOT FOUND\n",fpow_passed_event_to_str(queueEvent));

    return(found);
}

// static char *
// fpow_event_to_str(int event)
// {
//     switch(event)  {
//         case NO_WAN_BATTERY_EVENT:
//             return("BATTERY_NO_WAN");
//         case LOW_BATTERY_EVENT:
//             return("BATTERY_LOW");
//         case CRITICAL_BATTERY_EVENT:
//             return("BATTERY_CRITICAL");
//         case WAN_SWITCH_ON_EVENT:
//             return("WAN_SWITCH_ON");
//         case WAN_SWITCH_OFF_EVENT:
//             return("WAN_SWITCH_OFF");
//         case AC_PLUG_INSERTED_EVENT:
//             return("AC_INSERTED");
//         case AC_PLUG_REMOVED_EVENT:
//             return("AC_REMOVED");
//         case USB_PLUG_INSERTED_EVENT:
//             return("USB_PLUG_INSERTED");
//         case USB_PLUG_REMOVED_EVENT:
//             return("USB_PLUG_REMOVED");
//         case HEADPHONES_INSERTED_EVENT:
//             return("HEADPHONES_INSERTED");
//         case HEADPHONES_REMOVED_EVENT:
//             return("HEADPHONES_REMOVED");
//         case WAN_RING_EVENT:
//             return("WAN_RING");
//         case KEYLOCK_EVENT:
//             return("KEYLOCK_WAKE");
//         case AUDIO_ON_EVENT:
//             return("AUDIO_ON");
//         case AUDIO_OFF_EVENT:
//             return("AUDIO_OFF");
//         case POWER_SWITCH_ON_EVENT:
//             return("POWER_SWITCH_ON");
//         case POWER_SWITCH_OFF_EVENT:
//             return("POWER_SWITCH_OFF");
//         case MMC_EVENT:
//             return("MMC");
//         case VALID_PASSWORD_EVENT:
//             return("VALID_PASSWORD");
//         case INVALID_PASSWORD_EVENT:
//             return("INVALID_PASSWORD");
//         case TIMEOUT_EVENT:
//             return("T1");
//         case TIMEOUT2_EVENT:
//             return("T2");
//         case TIMEOUT3_EVENT:
//             return("T3");
//         case TIMEOUT_USB_WAKE_EVENT:
//             return("TUSB");
//         case WATERMARK_CHECK_EVENT:
//             return("VC");
//         case PRIME_EVENT:
//             return("PRIME");
//         case TPH_COMPLETE_EVENT:
//             return("TPH_COMP");
//         case USB_WAKE_EVENT:
//             return("USB_WAKE");
//         case WAN_SWITCH_SWEEPER_EVENT:
//             return("WAN_SWEEP");
//         case TWO_FINGER_SWEEPER_EVENT:
//             return("AF_SWEEP");
//         default:
//             return("KBS");
//     }
// }

/***************************************************************************
 * Routine:     find_event_in_queue
 * Purpose:     Searches the event queue for the given event
 * Paramters:   event - the FPOW_EVENT to search for
 * Returns:     0 - didn't find event in the queue
 *              1 - found the event in the queue
 ***************************************************************************/
int
find_event_in_queue(FPOW_EVENT event)
{
    unsigned char found = 0;
    unsigned long flags;

    if (queue_empty())  {
        return(0);
    }

    spin_lock_irqsave(&g_kbd_lock, flags);
    found = __find_event_in_queue(event);
    spin_unlock_irqrestore(&g_kbd_lock, flags);

    if (found)
        hextlog("find_event_in_queue(%s) FOUND IT\n",fpow_event_to_str(event));
    else
        hextlog("find_event_in_queue(%s) NOT FOUND\n",fpow_event_to_str(event));

    return(found);
}



int
__remove_events_in_queue(int event)
{
    unsigned char found = 0;
    int cur = queue->tail;
    int done = 0;

    // Nothing to search if queue is empty
    if (queue_empty()) 
        goto exit;

    do {
        if (cur == queue->head)
            done = 1;
        else if (queue->buf[cur] == event) {
            hextlog("remove_events_in_queue(%s) removing event\n",fpow_passed_event_to_str(event));
            __remove_event_index_from_queue(cur);
            found = 1;
        }
        else
            cur = (cur + 1) & (AUX_BUF_SIZE-1);
    } while (0 == done);

exit:
    return(found);
}


int remove_events_in_queue(int event)
{
    unsigned long flags;
    int rc=0;

    spin_lock_irqsave(&g_kbd_lock, flags);
    rc = __remove_events_in_queue(event);
    spin_unlock_irqrestore(&g_kbd_lock, flags);
    return(rc);
}


/*********************************************************
 * Routine:     __get_from_kdb_queue
 * Purpose:     Remove and return next event from the 
 *              Fiona keyboard / event queue.
 * Paramters:   none
 * Returns:     unsigned char - value of element (the event / key)
 *                      removed from queue.
 * Assumptions: 
 *  1) Queue is protected (g_kbd_lock is held already)
 *  2) There is at least one event in the queue
 *********************************************************/
static unsigned char
__get_from_kdb_queue(void)
{
    unsigned char result;
    result = queue->buf[queue->tail];
    queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
    return(result);
}

/*********************************************************
 * Routine:     get_from_queue
 * Purpose:     Remove next event from the Fiona keyboard / event
 *              queue.
 * Paramters:   none
 * Returns:     unsigned char - value of element (the event / key)
 *                      removed from queue.
 * Assumptions: There is at least one event in the queue
 *********************************************************/
static unsigned char
get_from_queue(void)
{
    unsigned char result;
    unsigned long flags;

    spin_lock_irqsave(&g_kbd_lock, flags);
        result = __get_from_kdb_queue();
    spin_unlock_irqrestore(&g_kbd_lock, flags);
    return result;
}

/*****************************************************************************
 * Power Management Routines
 *****************************************************************************/
#ifdef LOCAL_CONFIG_PM
static int
fiona_keyboard_suspend(u32 state)
{
    return(0);
} 


static int
fiona_keyboard_resume(u32 state)
{
    return(0);
} 

#else
#define fiona_keyboard_suspend  NULL
#define fiona_keyboard_resume   NULL
#endif  // LOCAL_CONFIG_PM


/*****************************************************************************
 * Core Driver 
 *****************************************************************************/

static int
fiona_kbd_open(struct inode *node, struct file *filep)
{
    flowLog("fiona_kbd_open()\n");
    
    if (g_fiona_kbd_used++)
        return 0;

    return 0;
}

static int fiona_kbd_close(struct inode *node, struct file *filep)
{
    flowLog("fiona_kbd_close()\n");

    // One less client
    g_fiona_kbd_used--;

    // If there are no more clients, clear the queue out....
    if (0 == g_fiona_kbd_used) {
        kbd_clear_init_queue();
    }

    return(0);
}

void fiona_kbd_report_key(int scode, int pressed)
{
    int the_key;
    int scancode = (scode & 0x000000FF);

    if (!g_fiona_kbd_used) {
        flowLog("Throwing away %s key code 0x%02x (scancode:0x%x), kbd driver not open\n",(pressed==0)?"Released":"Pressed",kbd_ioc_xlate[scancode],scancode);
        return;
    }

    if (scancode >= FIONA_KEYMAP_SIZE) {
        // It's a user event, translate event to an event-tied keycode
        if ((scancode >= FIONA_EVENT_FIRST) && (scancode <= FIONA_EVENT_LAST)) {
            the_key = get_fiona_event_code(scancode);     // Event is valid
            if (!pressed)
                the_key |= KEY_WAS_RELEASED;
        }
        else {
            printk("Fiona event scancode=%d is out-of-range !!\n",scancode);
            return;
        }

		hextlog("Queueing EVENT: %s (scancode = 0x%x, the_key = 0x%x)\n",fiona_event_keycode_to_str(scancode|(pressed?0:0x80)),scancode,the_key);
    }
    else  {
        // It's an actual key event...
        the_key = kbd_ioc_xlate[scancode];
        flowLog("Queued up 0x%x, %s\n",the_key,pressed?"PRESSED":"RELEASED");
        if (!pressed)
            the_key |= KEY_WAS_RELEASED;
    }
    
    // If we're faking the PNLCD on the eInk display, keep track of how many scrollwheel
    // up/down events have been queued.
    if (FAKE_PNLCD_IN_USE() && IS_SCROLL_WHEEL_MOTION_KEY(the_key & ~KEY_WAS_RELEASED)) {
        // Throw away any events that come in while we're updating the eInk display with
        // the fake PNLCD.
        if (FAKE_PNLCD_UPDATE()) {
            return;
        }   
        
        // Otherwise, count them when the Framework is running.
        if (FRAMEWORK_RUNNING()) {
            INC_PNLCD_EVENT_COUNT();
        }
    }

    // Queue the byte
    kdb_queue_byte(the_key);
}

// static int stalled_queue_for_ac_wake_already = 0;
// static int get_q_stalled_ac_wake(void)
// {
//     return(stalled_queue_for_ac_wake_already);
// }
// static void set_q_stalled_ac_wake(void)
// {
//     stalled_queue_for_ac_wake_already = 1;
// }

// static void clear_q_stalled_ac_wake(void)
// {
//     stalled_queue_for_ac_wake_already = 0;
// }
//     
// static int stall_q_for_ac_wake(void)
// {
//     if (get_q_stalled_ac_wake() == 0)  {
//         set_q_stalled_ac_wake();
//         return(1);
//     }
//     else {
//         return(0);  // We already stalled once for AC_IN this wake cycle,
//                     // no need to do it again....
//     }
// }

int do_pass_event_to_caller_hack(int the_key)
{
    int passItOn = 1;
    int do_stall_queue = 0;

    // If we get a Power Off key, we need to watch for a Power On key.
    // IF that key comes in before we actually go to sleep, we'll adjust for the
    // situation by NOT putting the unit to sleep and instead just processing the
    // Power On as if we had slept and just woke up from it....

    // This holds true for any event that's going to put us to sleep.  To avoid
    // events making it into the Java queue, we'll stall after passing any sleep
    // event upstream so that queue manipulation can happen as it needs to...

    // This is very important because microWindow's has an internal buffer that 
    // PM can't see, and microWindow's is the piece that passes events from
    // here up to Java Power Management, so if events get stuck in there, we
    // won't be able to see them...
    //
    // Kindle NOTE:  If you change this switch statement by adding or removing 
    //          cases, you must adjust the logic inside PowerManager.java's 
    //          eventQueueStallsForEvent() routine.
    //    
    switch(the_key)  {
        case EVENT_POWER_SWITCH_OFF:
        case EVENT_BATTERY_CRITICAL:
        case EVENT_TIMEOUT_2:
        case EVENT_TIMEOUT_3:
            do_stall_queue = 1;
            break;

        case EVENT_PRIME_PUMP:
			// NOTHING TO DO
            break;

        case EVENT_AC_INSERTED:
			// NOTHING TO DO
            break;

        default:
            break;
    }

    if (do_stall_queue) {
        hextlog("do_pass_event_to_caller_hack(): Stalling Queue due to %s\n",fpow_passed_event_to_str(the_key));
        setEventQueueStalled(1);
    }


    //
    // Remember the last event we passed up.
    // This is important because we have an event "limbo" state where
    // an event can be in the microwindow's piece when threads get swapped,
    // so when Java looks in it's queue and in the Kernel queue, it will miss
    // the one that could be sitting in "limbo".  By remembering the last one
    // we sent up (and exposing that to Java), the Java Power Management piece
    // can determine exactly which events are in the event "pipeline".
    //
    g_last_event_passed_to_java = the_key;

    return(passItOn);      // We want this one passed, and then the stall to take effect
}

/*******************************************************************
 * Routine:     handle_fiona_event
 * Purpose:     Converts eventcodes to events that the framework knows
 *              about and passes them up to the framework.
 *              It passes events to the framework through the keyboard
 *              driver.
 * Paramters:   eventcode - the eventcode for the event
 * Returns:     nont
 * Assumptions: none
 * Author:      Nick Vaccaro <nvaccaro@lab126.com>
 *******************************************************************/
void handle_fiona_event(unsigned char eventcode)
{
    fiona_kbd_report_key((eventcode&0x7F),((eventcode&0x80)==0));
}
EXPORT_SYMBOL(handle_fiona_event);

/*
 * Put bytes from input queue to user buffer.
 */
static ssize_t
fiona_kbd_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
    DECLARE_WAITQUEUE(wait, current);
    ssize_t i = count;
    unsigned char c;

    flowLog("fiona_kbd_read(%d bytes)\n",count);

    if (queue_empty() || isEventQueueStalled()) {
        if (isEventQueueStalled()) {
            flowLog("fiona_kbd_read() found Q stalled\n");
        }
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        add_wait_queue(&queue->proc_list, &wait);
repeat:
        set_current_state(TASK_INTERRUPTIBLE);
        if ((queue_empty() || isEventQueueStalled()) && !signal_pending(current)) {
            schedule();
            goto repeat;
        }
        current->state = TASK_RUNNING;
        remove_wait_queue(&queue->proc_list, &wait);
    }

    while (i > 0 && (!queue_empty() && !isEventQueueStalled())) {
        c = get_from_queue();
        if (do_pass_event_to_caller_hack(c)) {
            put_user(c, buffer++);
            i--;
            flowLog(KERN_INFO "fiona_kbd_read: passed read_byte 0x%x to Java\n", (int) c );
        }
    }
    if (count-i) {
        file->f_dentry->d_inode->i_atime = CURRENT_TIME;
        return count-i;
    }
    if (signal_pending(current))
        return -ERESTARTSYS;
    return 0;
}

/* push raw fiona events into the queue */
static ssize_t fiona_kbd_write(struct file * file, const char __user * buffer, size_t count, loff_t *ppos) {
	size_t i;
	for(i=0;i<count;i++) {
		kdb_queue_byte(buffer[i]);
	}
    return count;
}

/*
 * Poll to the aux device.
 */
static unsigned int
fiona_kbd_poll(struct file *file, poll_table * wait)
{
    poll_wait(file, &queue->proc_list, wait);
    if (!queue_empty() && !isEventQueueStalled())
    {
        flowLog(KERN_INFO "fiona_kbd_poll: kbd_poll != 0\n");
        return POLLIN | POLLRDNORM;
    }

    flowLog(KERN_INFO "fiona_kbd_poll: kbd_poll = 0\n");
    return 0;
}

static int __init fiona_kbd_init(void)
{
    int retVal = 0;

	kbd_ioc_xlate = kbd_ioc_xlate_dvt1; /* only use dvt1 keypads */

    retVal = misc_register(&keyboard_device);
    if( retVal < 0) {
        printk("Could not register keyboard handler as misc device !!\n");
    }
    else  {
        flowLog("Keyboard open, queue initialized\n");
        printk("KEYBOARD: Fiona Keyboard Driver %s\n",FIONA_KEYBOARD_DRIVER_VERSION);
        queue = (struct kbd_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
        kbd_clear_init_queue();
    }

#ifdef CONFIG_FIONA_PM_KEYBOARD
    // Register IOC Keyboard driver with Fiona Power Manager,
    // we're ignoring result here as we'll still allow driver to
    // work even if we can't do efficient power management...
    if (keyboard_register_with_fpow(NULL, &g_kbd_fpow_component_ptr))
        printk("KEYBOARD: Failed to register with fpow !!\n");
#endif

    return(0);
}

static void __exit fiona_kbd_exit(void)
{
    flowLog("fiona_kbd_exit()\n");

#ifdef CONFIG_FIONA_PM_KEYBOARD
    // Unregister IOC driver with Fiona Power Manager
    keyboard_unregister_with_fpow(g_kbd_fpow_component_ptr);
    g_kbd_fpow_component_ptr  = NULL;
#endif

    //input_unregister_device(&g_fiona_kbd_dev);
    misc_deregister(&keyboard_device);
    kfree(g_fiona_kbd_dev.name);
    kfree(queue);
}

MODULE_AUTHOR("Lab126, Inc.");
MODULE_DESCRIPTION("Fake out Fiona Keyboard /dev/misc/input device");
MODULE_LICENSE("GPL");

module_init(fiona_kbd_init);
module_exit(fiona_kbd_exit);
