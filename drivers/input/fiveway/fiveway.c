/*
** 5-Way Switch driver routine for Mario.  (C) 2008 Lab126
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/mach/irq.h>
#include <asm/segment.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/delay.h>

#include <asm/arch-mxc/mx31_pins.h>
#include <asm/arch-mxc/mx31.h>
#include <asm/arch-mxc/pmic_power.h>
#include <asm/arch/gpio.h>

// Logging
unsigned int logging_mask;
#define LLOG_G_LOG_MASK logging_mask
#define LLOG_KERNEL_COMP "fiveway"
#include <llog.h>

#include "fiveway.h"

// Input device structure
static struct input_dev *fiveway_dev = NULL;

// Global variables

static struct timer_list auto_repeat_timer;
static struct timer_list debounce_timer;
static char line_state[NUM_OF_LINES];

// Workqueue
static struct work_struct fiveway_debounce_tq;
static struct work_struct fiveway_auto_repeat_tq;
static void fiveway_debounce_wk(struct work_struct *);
static void fiveway_auto_repeat_wk(struct work_struct *);

// Proc entry structure and global variable.
#define FIVEWAY_PROC_FILE "fiveway"
#define PROC_CMD_LEN 50
static struct proc_dir_entry *proc_entry;
static int fiveway_lock = 0;  // 0=unlocked; non-zero=locked

// The following array contains information for the fiveway
// lines for each different hardware revision.
// A NULL value in any element indicates that the line is not a GPIO;
// it may be unused, or it may be connected to the keypad array, in which
// case the keyboard driver should take care of it.
static iomux_pin_name_t line_info[MAX_VERSIONS][MAX_LINES];

static iomux_pin_name_t fiveway_up_gpio;
static iomux_pin_name_t fiveway_down_gpio;
static iomux_pin_name_t fiveway_left_gpio;
static iomux_pin_name_t fiveway_right_gpio;
static iomux_pin_name_t fiveway_select_gpio;


// Module parameters

#define FIVEWAY_REV_DEFAULT 0
static int fiveway_rev = FIVEWAY_REV_DEFAULT;
module_param(fiveway_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fiveway_rev, "5-Way hardware revision; 0=Mario, 1=Turing prototype build, 2=Turing v1 (default is 0)");

#define DEBOUNCE_TIMEOUT_DEFAULT 20
static int debounce_timeout = DEBOUNCE_TIMEOUT_DEFAULT;
module_param(debounce_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debounce_timeout, "Time for debouncing the fiveway switch, in msec; MUST be a multiple of 10 (default is 100)");

#define HOLD_TIMEOUT_DEFAULT 1000
static int UP_hold_timeout = HOLD_TIMEOUT_DEFAULT;
module_param(UP_hold_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_hold_timeout, "Time before holding UP starts auto-repeat mode, in msec; MUST be a multiple of 10 (default is 1000; 0 = no auto-repeat)");

static int DOWN_hold_timeout = HOLD_TIMEOUT_DEFAULT;
module_param(DOWN_hold_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_hold_timeout, "Time before holding DOWN starts auto-repeat mode, in msec; MUST be a multiple of 10 (default is 1000; 0 = no auto-repeat)");

static int LEFT_hold_timeout = HOLD_TIMEOUT_DEFAULT;
module_param(LEFT_hold_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_hold_timeout, "Time before holding LEFT starts auto-repeat mode, in msec; MUST be a multiple of 10 (default is 1000; 0 = no auto-repeat)");

static int RIGHT_hold_timeout = HOLD_TIMEOUT_DEFAULT;
module_param(RIGHT_hold_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_hold_timeout, "Time before holding RIGHT starts auto-repeat mode, in msec; MUST be a multiple of 10 (default is 1000; 0 = no auto-repeat)");

static int SELECT_hold_timeout = 1000;
module_param(SELECT_hold_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(SELECT_hold_timeout, "Time before holding SELECT generates HOLD event or starts auto-repeat mode, in msec; MUST be a multiple of 10 (default is 1000)");

#define AUTO_REPEAT_TIMEOUT_DEFAULT 300
static int UP_auto_repeat_time = AUTO_REPEAT_TIMEOUT_DEFAULT;
module_param(UP_auto_repeat_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_auto_repeat_time, "Time between UP key auto-repeat events; MUST be a multiple of 10 (default is 300; 0 = no auto-repeat)");

static int DOWN_auto_repeat_time = AUTO_REPEAT_TIMEOUT_DEFAULT;
module_param(DOWN_auto_repeat_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_auto_repeat_time, "Time between DOWN key auto-repeat events; MUST be a multiple of 10 (default is 300; 0 = no auto-repeat)");

static int LEFT_auto_repeat_time = AUTO_REPEAT_TIMEOUT_DEFAULT;
module_param(LEFT_auto_repeat_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_auto_repeat_time, "Time between LEFT key auto-repeat events; MUST be a multiple of 10 (default is 300; 0 = no auto-repeat)");

static int RIGHT_auto_repeat_time = AUTO_REPEAT_TIMEOUT_DEFAULT;
module_param(RIGHT_auto_repeat_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_auto_repeat_time, "Time between RIGHT key auto-repeat events; MUST be a multiple of 10 (default is 300; 0 = no auto-repeat)");

static int SELECT_auto_repeat_time = AUTO_REPEAT_TIMEOUT_DEFAULT;
module_param(SELECT_auto_repeat_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(SELECT_auto_repeat_time, "Time between SELECT key auto-repeat events; MUST be a multiple of 10 (default is 300; 0 = no auto-repeat)");

static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");


/*
 * Set the log mask depending on the debug level
 */
static void set_debug_log_mask(int debug_level)
{
   logging_mask = LLOG_LEVEL_MASK_INFO | LLOG_LEVEL_MASK_EXCEPTIONS;
   if (debug_level < 1) return;
   logging_mask |= LLOG_LEVEL_DEBUG0;
   if (debug_level < 2) return;
   logging_mask |= LLOG_LEVEL_DEBUG1;
   if (debug_level < 3) return;
   logging_mask |= LLOG_LEVEL_DEBUG2;
   if (debug_level < 4) return;
   logging_mask |= LLOG_LEVEL_DEBUG3;
   if (debug_level < 5) return;
   logging_mask |= LLOG_LEVEL_DEBUG4;
   if (debug_level < 6) return;
   logging_mask |= LLOG_LEVEL_DEBUG5;
   if (debug_level < 7) return;
   logging_mask |= LLOG_LEVEL_DEBUG6;
   if (debug_level < 8) return;
   logging_mask |= LLOG_LEVEL_DEBUG7;
   if (debug_level < 9) return;
   logging_mask |= LLOG_LEVEL_DEBUG8;
   if (debug_level < 10) return;
   logging_mask |= LLOG_LEVEL_DEBUG9;
}


/*
 * Report the fiveway event.  In addition to calling
 * input_event, this function sends a uevent for any
 * processes listening in the userspace
 */
static void report_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
   input_event( dev, type, code, value );

   // Send uevent so userspace power daemon can monitor
   // fiveway activity
   // NOTE: only sending uevent on key down as per Manish

   if (value == 1) {
      schedule();
      if( kobject_uevent( &fiveway_dev->cdev.kobj, KOBJ_CHANGE ) )
      {
          LLOG_ERROR("uevent", "", "Fiveway driver failed to send uevent\n" );
      }
   }
}


static void auto_repeat_timer_timeout(unsigned long timer_data)
{
   schedule_work(&fiveway_auto_repeat_tq);
   //fiveway_auto_repeat_wk(&fiveway_auto_repeat_tq);
}


static void fiveway_auto_repeat_wk(struct work_struct *work)
{
   int timeout;

   LLOG_DEBUG0("Got auto-repeat timeout\n");
   // Send appropriate event, depending on button held
   if ( mxc_get_gpio_datain(fiveway_up_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch UP signal; sending FIVEWAY_KEYCODE_UP\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_UP, 2);
      timeout = UP_auto_repeat_time;
      line_state[UP_LINE] = LINE_HELD;
   }
   else if ( mxc_get_gpio_datain(fiveway_down_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch DOWN signal; sending FIVEWAY_KEYCODE_DOWN\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_DOWN, 2);
      timeout = DOWN_auto_repeat_time;
      line_state[DOWN_LINE] = LINE_HELD;
   }
   else if ( mxc_get_gpio_datain(fiveway_left_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch LEFT signal; sending FIVEWAY_KEYCODE_LEFT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_LEFT, 2);
      timeout = LEFT_auto_repeat_time;
      line_state[LEFT_LINE] = LINE_HELD;
   }
   else if ( mxc_get_gpio_datain(fiveway_right_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch RIGHT signal; sending FIVEWAY_KEYCODE_RIGHT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_RIGHT, 2);
      timeout = RIGHT_auto_repeat_time;
      line_state[RIGHT_LINE] = LINE_HELD;
   }
   else if ( mxc_get_gpio_datain(fiveway_select_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch SELECT signal; sending FIVEWAY_KEYCODE_SELECT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_SELECT, 2);
      timeout = SELECT_auto_repeat_time;
      line_state[SELECT_LINE] = LINE_HELD;
   }
   else {
      // Do not start the auto-repeat timer because no more buttons are pressed
      timeout = 0;
   }

   if (timeout == 0) {
      // Auto-repeat function disabled or button not pressed any more
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 0;
   }
   else {
      // Start the auto-repeat timer again, with the repeat-key parameter
      // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.expires = jiffies + ((timeout * HZ) / 1000);
      add_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 1; // Indicate that timer is running

      // Start the haptic device to provide feedback on auto-repeat
      // NOTE: FUNCTIONALITY TAKEN OUT FOR TURING
      // do_thump();
   }
}


static void debounce_timer_timeout(unsigned long timer_data)
{
   schedule_work(&fiveway_debounce_tq);
   //fiveway_debounce_wk(&fiveway_debounce_tq);
}


static void fiveway_debounce_wk(struct work_struct *work)
{
   int timeout = 0;

   LLOG_DEBUG1("Debounce period expired\n");
   // Send appropriate event, depending on button held
   if ( mxc_get_gpio_datain(fiveway_up_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch UP signal; sending FIVEWAY_KEYCODE_UP\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_UP, 1);
      timeout = UP_hold_timeout;
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(FIVEWAY_UP_IRQ, IRQF_TRIGGER_RISING);
      line_state[UP_LINE] = LINE_ASSERTED;
   }
   else if ( mxc_get_gpio_datain(fiveway_down_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch DOWN signal; sending FIVEWAY_KEYCODE_DOWN\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_DOWN, 1);
      timeout = DOWN_hold_timeout;
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(FIVEWAY_DOWN_IRQ, IRQF_TRIGGER_RISING);
      line_state[DOWN_LINE] = LINE_ASSERTED;
   }
   else if ( mxc_get_gpio_datain(fiveway_left_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch LEFT signal; sending FIVEWAY_KEYCODE_LEFT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_LEFT, 1);
      timeout = LEFT_hold_timeout;
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(FIVEWAY_LEFT_IRQ, IRQF_TRIGGER_RISING);
      line_state[LEFT_LINE] = LINE_ASSERTED;
   }
   else if ( mxc_get_gpio_datain(fiveway_right_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch RIGHT signal; sending FIVEWAY_KEYCODE_RIGHT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_RIGHT, 1);
      timeout = RIGHT_hold_timeout;
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(FIVEWAY_RIGHT_IRQ, IRQF_TRIGGER_RISING);
      line_state[RIGHT_LINE] = LINE_ASSERTED;
   }
   else if ( mxc_get_gpio_datain(fiveway_select_gpio) == 0 ) {
      LLOG_DEBUG0("Got 5-Way Switch SELECT signal; sending FIVEWAY_KEYCODE_SELECT\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_SELECT, 1);
      timeout = SELECT_hold_timeout;
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(FIVEWAY_SELECT_IRQ, IRQF_TRIGGER_RISING);
      line_state[SELECT_LINE] = LINE_ASSERTED;
   }

   // Check if any key was released
   if (( mxc_get_gpio_datain(fiveway_up_gpio) == 1 ) &&
       ( line_state[UP_LINE] != LINE_DEASSERTED )) {
      LLOG_DEBUG0("5-Way Switch UP was released\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_UP, 0);
      line_state[UP_LINE] = LINE_DEASSERTED;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(FIVEWAY_UP_IRQ, IRQF_TRIGGER_FALLING);
   }
   if (( mxc_get_gpio_datain(fiveway_down_gpio) == 1 ) &&
       ( line_state[DOWN_LINE] != LINE_DEASSERTED )) {
      LLOG_DEBUG0("5-Way Switch DOWN was released\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_DOWN, 0);
      line_state[DOWN_LINE] = LINE_DEASSERTED;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(FIVEWAY_DOWN_IRQ, IRQF_TRIGGER_FALLING);
   }
   if (( mxc_get_gpio_datain(fiveway_left_gpio) == 1 ) &&
       ( line_state[LEFT_LINE] != LINE_DEASSERTED )) {
      LLOG_DEBUG0("5-Way Switch LEFT was released\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_LEFT, 0);
      line_state[LEFT_LINE] = LINE_DEASSERTED;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(FIVEWAY_LEFT_IRQ, IRQF_TRIGGER_FALLING);
   }
   if (( mxc_get_gpio_datain(fiveway_right_gpio) == 1 ) &&
       ( line_state[RIGHT_LINE] != LINE_DEASSERTED )) {
      LLOG_DEBUG0("5-Way Switch RIGHT was released\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_RIGHT, 0);
      line_state[RIGHT_LINE] = LINE_DEASSERTED;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(FIVEWAY_RIGHT_IRQ, IRQF_TRIGGER_FALLING);
   }
   if (( mxc_get_gpio_datain(fiveway_select_gpio) == 1 ) &&
       ( line_state[SELECT_LINE] != LINE_DEASSERTED )) {
      LLOG_DEBUG0("5-Way Switch SELECT was released\n");
      report_event(fiveway_dev, EV_KEY, FIVEWAY_KEYCODE_SELECT, 0);
      line_state[SELECT_LINE] = LINE_DEASSERTED;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(FIVEWAY_SELECT_IRQ, IRQF_TRIGGER_FALLING);
   }

   // Indicate that the debounce timer is not running
   del_timer(&debounce_timer);
   debounce_timer.data = 0;

   if (timeout == 0) {
      // Auto-repeat function disabled 
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 0;
   }
   else {
      // Start the auto-repeat timer, with the appropriate HOLD parameter
      // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.expires = jiffies + ((timeout * HZ) / 1000);
      add_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 1; // Indicate that timer is running
   }
}


static irqreturn_t fiveway_irq (int irq, void *data, struct pt_regs *r)
{
   // If the debounce timer is running, ignore the UP signal
   if (debounce_timer.data != 0) {
      LLOG_DEBUG0("Ignoring 5-Way Switch signal while debouncing switch\n");
      return IRQ_HANDLED;
   }

   // Start the debounce timer, to avoid spurious signals
   LLOG_DEBUG1("Starting debounce_timer...\n");
   // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
   debounce_timer.expires = jiffies + ((debounce_timeout * HZ) / 1000);
   mod_timer(&debounce_timer, debounce_timer.expires);
   debounce_timer.data = 1; // Indicate that timer is running

   return IRQ_HANDLED;
}


/*!
 * This function puts the 5-Way driver in low-power mode/state,
 * by disabling interrupts from the input lines.
 */
void fiveway_off(void)
{
   LLOG_DEBUG0("Stopping timers; disabling IRQs and pullups...\n");

   // Stop fiveway timers if running
   if (auto_repeat_timer.data != 0) {
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 0;
   }
   if (debounce_timer.data != 0) {
      del_timer(&debounce_timer);
      debounce_timer.data = 0;
   }

   // Disable IRQ lines and turn off pullups on GPIO output lines
   if (fiveway_up_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(FIVEWAY_UP_IRQ);
      mario_disable_pullup_pad(fiveway_up_gpio);
   }
   if (fiveway_down_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(FIVEWAY_DOWN_IRQ);
      mario_disable_pullup_pad(fiveway_down_gpio);
   }
   if (fiveway_left_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(FIVEWAY_LEFT_IRQ);
      mario_disable_pullup_pad(fiveway_left_gpio);
   }
   if (fiveway_right_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(FIVEWAY_RIGHT_IRQ);
      mario_disable_pullup_pad(fiveway_right_gpio);
   }
   if (fiveway_select_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(FIVEWAY_SELECT_IRQ);
      mario_disable_pullup_pad(fiveway_select_gpio);
   }
}


/*!
 * This function puts the 5-Way driver in low-power mode/state,
 * by disabling interrupts from the input lines.
 * @param   pdev  the device structure used to give information on 5-Way
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function always returns 0.
 */
static int fiveway_suspend(struct platform_device *pdev, pm_message_t state)
{
   fiveway_off();

   return 0;
}


/*!
 * This function brings the 5-Way driver back from low-power state,
 * by re-enabling the line interrupts.
 *
 * @return  The function always returns 0.
 */
void fiveway_on(void)
{
   LLOG_DEBUG0("Enabling IRQs and pullups...\n");

   // Turn off pullups on GPIO output lines and enable IRQs
   if (fiveway_up_gpio != (iomux_pin_name_t)NULL) {
      mario_enable_pullup_pad(fiveway_up_gpio);
      enable_irq(FIVEWAY_UP_IRQ);
   }
   if (fiveway_down_gpio != (iomux_pin_name_t)NULL) {
      mario_enable_pullup_pad(fiveway_down_gpio);
      enable_irq(FIVEWAY_DOWN_IRQ);
   }
   if (fiveway_left_gpio != (iomux_pin_name_t)NULL) {
      mario_enable_pullup_pad(fiveway_left_gpio);
      enable_irq(FIVEWAY_LEFT_IRQ);
   }
   if (fiveway_right_gpio != (iomux_pin_name_t)NULL) {
      mario_enable_pullup_pad(fiveway_right_gpio);
      enable_irq(FIVEWAY_RIGHT_IRQ);
   }
   if (fiveway_select_gpio != (iomux_pin_name_t)NULL) {
      mario_enable_pullup_pad(fiveway_select_gpio);
      enable_irq(FIVEWAY_SELECT_IRQ);
   }
}


/*!
 * This function brings the 5-Way driver back from low-power state,
 * by re-enabling the line interrupts.
 *
 * @param   pdev  the device structure used to give information on Keypad
 *                to resume
 *
 * @return  The function always returns 0.
 */
static int fiveway_resume(struct platform_device *pdev)
{
   fiveway_on();

   return 0;
}


/*!
 * This function is called when the fiveway driver is opened.
 * Since fiveway initialization is done in __init, nothing is done in open.
 *
 * @param    dev    Pointer to device inode
 *
 * @result    The function always returns 0
 */
static int fiveway_open(struct input_dev *dev)
{
        return 0;
}

/*!
 * This function is called close the fiveway device.
 * Nothing is done in this function, since every thing is taken care in
 * __exit function.
 *
 * @param    dev    Pointer to device inode
 *
 */
static void fiveway_close(struct input_dev *dev)
{
}

/*
 * This function returns the current state of the fiveway device
 */
int fiveway_proc_read( char *page, char **start, off_t off,
                      int count, int *eof, void *data )
{
   int len;

   if (off > 0) {
     *eof = 1;
     return 0;
   }
   if (fiveway_lock == 0) {
      len = sprintf(page, "fiveway is unlocked\n");
   }
   else {
      len = sprintf(page, "fiveway is locked\n");
   }
   return len;
}


/*
 * This function executes fiveway control commands based on the input string.
 *    unlock = unlock the fiveway
 *    lock   = lock the fiveway
 *    send   = send a fiveway event, simulating a line detection
 *    debug  = set the debug level for logging messages (0=off)
 */
ssize_t fiveway_proc_write( struct file *filp, const char __user *buff,
                            unsigned long len, void *data )

{
   char command[PROC_CMD_LEN];
   int  keycode;

   if (len > PROC_CMD_LEN) {
      LLOG_ERROR("proc_len", "", "Fiveway command is too long!\n");
      return -ENOSPC;
   }

   if (copy_from_user(command, buff, len)) {
      return -EFAULT;
   }

   if ( !strncmp(command, "unlock", 6) ) {
      // Requested to unlock fiveway
      if (fiveway_lock == 0) {
         // Fiveway was already unlocked; do nothing
         LLOG_INFO("unlock1", "status=unlocked", "\n");
      }
      else {
         // Unlock fiveway
         fiveway_on();
         fiveway_lock = 0;
         LLOG_INFO("unlock2", "status=unlocked", "\n");
      }
   }
   else if ( !strncmp(command, "lock", 4) ) {
      // Requested to lock fiveway
      if (fiveway_lock != 0) {
         // Fiveway was already locked; do nothing
         LLOG_INFO("locked1", "status=locked", "\n");
      }
      else {
         // Lock fiveway
         fiveway_off();
         fiveway_lock = 1;
         LLOG_INFO("locked2", "status=locked", "\n");
      }
   }
   else if ( !strncmp(command, "send", 4) ) {
      // Requested to send a keycode
      if (fiveway_lock != 0) {
         // Keypad is locked; do nothing
         LLOG_WARN("locked3", "", "The fiveway device is locked!  Unlock before sending keycodes.\n");
      }
      else {
         // Read keycode and send
         sscanf(command, "send %d", &keycode);
         LLOG_INFO("send_key", "", "Sending keycode %d\n", keycode);
         report_event(fiveway_dev, EV_KEY, keycode, 1);
         report_event(fiveway_dev, EV_KEY, keycode, 0);
      }
   }
   else if ( !strncmp(command, "debug", 5) ) {
      // Requested to set debug level
      sscanf(command, "debug %d", &debug);
      LLOG_INFO("debug", "", "Setting debug level to %d\n", debug);
      set_debug_log_mask(debug);
   }
   else {
      LLOG_ERROR("proc_cmd", "", "Unrecognized fiveway command\n");
   }

   return len;
}


/*!
 * This function is called during the driver binding process.
 *
 * @param   pdev  the device structure used to store device specific
 *                information that is used by the suspend, resume and remove
 *                functions.
 *
 * @return  The function returns 0 on successful registration. Otherwise returns
 *          specific error code.
 */
static int __devinit fiveway_probe(struct platform_device *pdev)
{
   int rqstatus = 0;
   int error;
   int i;

   LLOG_INFO("probe0", "", "Starting...\n");

   /* Create proc entry */
   proc_entry = create_proc_entry(FIVEWAY_PROC_FILE, 0644, NULL);
   if (proc_entry == NULL) {
      LLOG_ERROR("probe1", "", "Fiveway could not create proc entry\n");
      return -ENOMEM;
   } else {
      proc_entry->read_proc = fiveway_proc_read;
      proc_entry->write_proc = fiveway_proc_write;
      proc_entry->owner = THIS_MODULE;
   }

   // Initialize the array that contains the information for the
   // fiveway lines of the various hardware platforms
   line_info[MARIO][FIVEWAY_UP]     = MARIO_FIVEWAY_UP_GPIO;
   line_info[MARIO][FIVEWAY_DOWN]   = MARIO_FIVEWAY_DOWN_GPIO;
   line_info[MARIO][FIVEWAY_LEFT]   = MARIO_FIVEWAY_LEFT_GPIO;
   line_info[MARIO][FIVEWAY_RIGHT]  = MARIO_FIVEWAY_RIGHT_GPIO;
   line_info[MARIO][FIVEWAY_SELECT] = MARIO_FIVEWAY_SELECT_GPIO;

   line_info[TURING_V0][FIVEWAY_UP]     = TURING_V0_FIVEWAY_UP_GPIO;
   line_info[TURING_V0][FIVEWAY_DOWN]   = TURING_V0_FIVEWAY_DOWN_GPIO;
   line_info[TURING_V0][FIVEWAY_LEFT]   = TURING_V0_FIVEWAY_LEFT_GPIO;
   line_info[TURING_V0][FIVEWAY_RIGHT]  = TURING_V0_FIVEWAY_RIGHT_GPIO;
   line_info[TURING_V0][FIVEWAY_SELECT] = TURING_V0_FIVEWAY_SELECT_GPIO;

   line_info[NELL_TURING_V1][FIVEWAY_UP]     = NELL_TURING_V1_FIVEWAY_UP_GPIO;
   line_info[NELL_TURING_V1][FIVEWAY_DOWN]   = NELL_TURING_V1_FIVEWAY_DOWN_GPIO;
   line_info[NELL_TURING_V1][FIVEWAY_LEFT]   = NELL_TURING_V1_FIVEWAY_LEFT_GPIO;
   line_info[NELL_TURING_V1][FIVEWAY_RIGHT]  = NELL_TURING_V1_FIVEWAY_RIGHT_GPIO;
   line_info[NELL_TURING_V1][FIVEWAY_SELECT] = NELL_TURING_V1_FIVEWAY_SELECT_GPIO;

   // Set dinamically the line informarion depending on the version passed
   // as a parameter
   if ((fiveway_rev >= 0) && (fiveway_rev < MAX_VERSIONS)) {
      fiveway_up_gpio     = line_info[fiveway_rev][FIVEWAY_UP];
      fiveway_down_gpio   = line_info[fiveway_rev][FIVEWAY_DOWN];
      fiveway_left_gpio   = line_info[fiveway_rev][FIVEWAY_LEFT];
      fiveway_right_gpio  = line_info[fiveway_rev][FIVEWAY_RIGHT];
      fiveway_select_gpio = line_info[fiveway_rev][FIVEWAY_SELECT];
   }
   else {
      LLOG_ERROR("bad_rev", "", "Fiveway_rev=%d is invalid; must be 0-%d\n", fiveway_rev, (MAX_VERSIONS-1));
      fiveway_up_gpio     = (iomux_pin_name_t) NULL;
      fiveway_down_gpio   = (iomux_pin_name_t) NULL;
      fiveway_left_gpio   = (iomux_pin_name_t) NULL;
      fiveway_right_gpio  = (iomux_pin_name_t) NULL;
      fiveway_select_gpio = (iomux_pin_name_t) NULL;

      return -1;
   }

   // Initialize the line state array
   for (i = 0; i < NUM_OF_LINES; i++) {
      line_state[i] = LINE_DEASSERTED;
   }

   // Initialize the auto-repeat timer structure
   init_timer(&auto_repeat_timer);  // Add timer to kernel list of timers
   auto_repeat_timer.data = 0;  // Timer currently not running
   auto_repeat_timer.function = auto_repeat_timer_timeout;

   // Initialize the debounce timer structure (used for debouncing the pushbuttons)
   init_timer(&debounce_timer);  // Add timer to kernel list of timers
   debounce_timer.data = 0;  // Timer currently not running
   debounce_timer.function = debounce_timer_timeout;

   // Set IRQ for fiveway GPIO lines; trigger on falling edges
   // Set the fiveway GPIO lines as inputs with falling-edge detection,
   // and register the IRQ service routines
   if (fiveway_up_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(FIVEWAY_UP_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(FIVEWAY_UP_IRQ, (irq_handler_t) fiveway_irq, 0, "fiveway", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("up_irq", "", "UP IRQ line = 0x%X; request status = %d\n", FIVEWAY_UP_IRQ, rqstatus);
      }
      if (mxc_request_iomux(fiveway_up_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("up_gpio", "", "Could not obtain GPIO pin for fiveway UP line\n");
      }
      else {
          mario_enable_pullup_pad(fiveway_up_gpio);
          mxc_set_gpio_direction(fiveway_up_gpio, 1);
      }
   }
   else {
      LLOG_WARN("up_null", "", "Fiveway UP GPIO defined as NULL\n");
   }

   if (fiveway_down_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(FIVEWAY_DOWN_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(FIVEWAY_DOWN_IRQ, (irq_handler_t) fiveway_irq, 0, "fiveway", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("down_irq", "", "DOWN IRQ line = 0x%X; request status = %d\n", FIVEWAY_DOWN_IRQ, rqstatus);
      }
      if (mxc_request_iomux(fiveway_down_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("down_gpio", "", "Could not obtain GPIO pin for fiveway DOWN line\n");
      }
      else {
          mario_enable_pullup_pad(fiveway_down_gpio);
          mxc_set_gpio_direction(fiveway_down_gpio, 1);
      }
   }
   else {
      LLOG_WARN("down_null", "", "Fiveway DOWN GPIO defined as NULL\n");
   }

   if (fiveway_left_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(FIVEWAY_LEFT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(FIVEWAY_LEFT_IRQ, (irq_handler_t) fiveway_irq, 0, "fiveway", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("left_irq", "", "LEFT IRQ line = 0x%X; request status = %d\n", FIVEWAY_LEFT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(fiveway_left_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("left_gpio", "", "Could not obtain GPIO pin for fiveway LEFT line\n");
      }
      else {
          mario_enable_pullup_pad(fiveway_left_gpio);
          mxc_set_gpio_direction(fiveway_left_gpio, 1);
      }
   }
   else {
      LLOG_WARN("left_null", "", "Fiveway LEFT GPIO defined as NULL\n");
   }

   if (fiveway_right_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(FIVEWAY_RIGHT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(FIVEWAY_RIGHT_IRQ, (irq_handler_t) fiveway_irq, 0, "fiveway", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("right_irq", "", "RIGHT IRQ line = 0x%X; request status = %d\n", FIVEWAY_RIGHT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(fiveway_right_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("right_gpio", "", "Could not obtain GPIO pin for fiveway RIGHT line\n");
      }
      else {
          mario_enable_pullup_pad(fiveway_right_gpio);
          mxc_set_gpio_direction(fiveway_right_gpio, 1);
      }
   }
   else {
      LLOG_WARN("right_null", "", "Fiveway RIGHT GPIO defined as NULL\n");
   }

   if (fiveway_select_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(FIVEWAY_SELECT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(FIVEWAY_SELECT_IRQ, (irq_handler_t) fiveway_irq, 0, "fiveway", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("select_irq", "", "SELECT IRQ line = 0x%X; request status = %d\n", FIVEWAY_SELECT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(fiveway_select_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("select_gpio", "", "Could not obtain GPIO pin for fiveway SELECT line\n");
      }
      else {
          mario_enable_pullup_pad(fiveway_select_gpio);
          mxc_set_gpio_direction(fiveway_select_gpio, 1);
      }
   }
   else {
      LLOG_WARN("select_null", "", "Fiveway SELECT GPIO defined as NULL\n");
   }

   LLOG_INFO("probe_done", "", "GPIOs and IRQs have been set up\n");

   // Set up the device file
   fiveway_dev = input_allocate_device();
   if (!fiveway_dev) {
      LLOG_ERROR("allocate_dev", "", "Not enough memory for input device\n");
      return -ENOMEM;
   }

   fiveway_dev->name = "fiveway";
   fiveway_dev->id.bustype = BUS_HOST;
   fiveway_dev->open = fiveway_open;
   fiveway_dev->close = fiveway_close;
   fiveway_dev->evbit[0] = BIT(EV_KEY);
   set_bit(FIVEWAY_KEYCODE_UP, fiveway_dev->keybit);
   set_bit(FIVEWAY_KEYCODE_DOWN, fiveway_dev->keybit);
   set_bit(FIVEWAY_KEYCODE_LEFT, fiveway_dev->keybit);
   set_bit(FIVEWAY_KEYCODE_RIGHT, fiveway_dev->keybit);
   set_bit(FIVEWAY_KEYCODE_SELECT, fiveway_dev->keybit);

   // Initialize work items
   INIT_WORK(&fiveway_debounce_tq, fiveway_debounce_wk);
   INIT_WORK(&fiveway_auto_repeat_tq, fiveway_auto_repeat_wk);
   
   error = input_register_device(fiveway_dev);
   if (error) {
      LLOG_ERROR("reg_dev", "", "Failed to register device\n");
      input_free_device(fiveway_dev);
      return error;
   }

   return 0;
}


/*!
 * Dissociates the driver from the fiveway device.
 *
 * @param   pdev  the device structure used to give information on which SDHC
 *                to remove
 *
 * @return  The function always returns 0.
 */
static int __devexit fiveway_remove(struct platform_device *pdev)
{
   // Release IRQs and GPIO pins; disable pullups on output GPIOs
   if (fiveway_up_gpio != (iomux_pin_name_t)NULL) {
      free_irq(FIVEWAY_UP_IRQ, NULL);
      mario_disable_pullup_pad(fiveway_up_gpio);
      mxc_free_iomux(fiveway_up_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }
   if (fiveway_down_gpio != (iomux_pin_name_t)NULL) {
      free_irq(FIVEWAY_DOWN_IRQ, NULL);
      mario_disable_pullup_pad(fiveway_down_gpio);
      mxc_free_iomux(fiveway_down_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }
   if (fiveway_left_gpio != (iomux_pin_name_t)NULL) {
      free_irq(FIVEWAY_LEFT_IRQ, NULL);
      mario_disable_pullup_pad(fiveway_left_gpio);
      mxc_free_iomux(fiveway_left_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }
   if (fiveway_right_gpio != (iomux_pin_name_t)NULL) {
      free_irq(FIVEWAY_RIGHT_IRQ, NULL);
      mario_disable_pullup_pad(fiveway_right_gpio);
      mxc_free_iomux(fiveway_right_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }
   if (fiveway_select_gpio != (iomux_pin_name_t)NULL) {
      free_irq(FIVEWAY_SELECT_IRQ, NULL);
      mario_disable_pullup_pad(fiveway_select_gpio);
      mxc_free_iomux(fiveway_select_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }

   // Stop fiveway timers if running
   if (auto_repeat_timer.data != 0) {
      del_timer(&auto_repeat_timer);
      auto_repeat_timer.data = 0;
   }
   if (debounce_timer.data != 0) {
      del_timer(&debounce_timer);
      debounce_timer.data = 0;
   }

   // Delete work items
   //destroy_workqueue(fiveway_debounce_tq);
   //destroy_workqueue(fiveway_auto_repeat_tq);

   remove_proc_entry(FIVEWAY_PROC_FILE, &proc_root);
   input_unregister_device(fiveway_dev);

   LLOG_INFO("exit", "", "IRQs released and timers stopped.\n");

   return 0;
}


/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver fiveway_driver = {
        .driver = {
                   .name = "fiveway",
                   .owner  = THIS_MODULE,
                   //.bus = &platform_bus_type,
                   },
        .suspend = fiveway_suspend,
        .resume = fiveway_resume,
        .probe = fiveway_probe,
        .remove = __devexit_p(fiveway_remove),
};

/*!
 * This function is called for module initialization.
 * It registers the fiveway char driver.
 *
 * @return      0 on success and a non-zero value on failure.
 */
static int __init fiveway_init(void)
{
	LLOG_INIT();
	set_debug_log_mask(debug);
        platform_driver_register(&fiveway_driver);
        fiveway_probe(NULL);
        LLOG_INFO("drv", "", "Fiveway driver loaded\n");
        return 0;
}

/*!
 * This function is called whenever the module is removed from the kernel. It
 * unregisters the fiveway driver from kernel.
 */
static void __exit fiveway_exit(void)
{
        fiveway_remove(NULL);
        platform_driver_unregister(&fiveway_driver);
}



module_init(fiveway_init);
module_exit(fiveway_exit);

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("5-Way Input Device Driver");
MODULE_LICENSE("GPL");
