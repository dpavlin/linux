/*
** Trackball driver routine for Fiona.  (C) 2007 Lab126
*/

#undef DEBUG_TRACKBALL

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
#include <linux/platform_device.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/mach/irq.h>
#include <asm/segment.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/delay.h>

#include <asm/arch-mxc/mx31_pins.h>
#include <asm/arch/gpio.h>

//#include <asm/arch/boot_globals.h>


//#include <asm/arch/fiona.h>
//#include <asm/arch/fpow.h>
//#include <asm/arch/pxa-regs.h>

// Event keycodes and handler routine are here:
#include "../keyboard/fiona_legacy/fiona_eventmap.h"

#include "trackball.h"

//typedef unsigned int u32;
typedef u32 * volatile reg_addr_t;

// Global variables

static int trackball_UP_counter = 0;
static int trackball_DOWN_counter = 0;
static int trackball_LEFT_counter = 0;
static int trackball_RIGHT_counter = 0;

static struct timer_list trackball_timer;
static struct timer_list hold_timer;
static struct timer_list release_debounce_timer;

// The following array contains information for the trackball
// lines for each different hardware revision.
// A NULL value in any element indicates that the line is not a GPIO;
// it may be unused, or it may be connected to the keypad array, in which
// case the keyboard driver should take care of it.
static iomux_pin_name_t line_info[MAX_VERSIONS][MAX_LINES];

static iomux_pin_name_t trbl_up_gpio;
static iomux_pin_name_t trbl_down_gpio;
static iomux_pin_name_t trbl_left_gpio;
static iomux_pin_name_t trbl_right_gpio;
static iomux_pin_name_t trbl_select_gpio;

// Module parameters
static int scroll_rev = 0;
module_param(scroll_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(scroll_rev, "Trackball hardware revision; 0=Mario, 1=Turing prototype build (default is 0)");

static int debug_trackball = 0;
module_param(debug_trackball, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");

static int UP_sample_window = 200;
module_param(UP_sample_window, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_sample_window, "Trackball UP GPIO sampling window, in msec; MUST be a multiple of 10 (default is 200)");

static int DOWN_sample_window = 200;
module_param(DOWN_sample_window, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_sample_window, "Trackball DOWN GPIO sampling window, in msec; MUST be a multiple of 10 (default is 200)");

static int LEFT_sample_window = 200;
module_param(LEFT_sample_window, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_sample_window, "Trackball LEFT GPIO sampling window, in msec; MUST be a multiple of 10 (default is 200)");

static int RIGHT_sample_window = 200;
module_param(RIGHT_sample_window, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_sample_window, "Trackball RIGHT GPIO sampling window, in msec; MUST be a multiple of 10 (default is 200)");

static int angle_percent = 30;
module_param(angle_percent, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(angle_percent, "Trackball calibration angle, in percent; SHOULD be between 1 and 49 (default is 30)");

static int UP_normal_accel = 2;
module_param(UP_normal_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_normal_accel, "Trackball minimum number of events to adjust sensitivity (default is 2)");

static int UP_normal_events = 1;
module_param(UP_normal_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_normal_events, "Number of UP events sent for normal acceleration (default is 1)");

static int UP_medium_accel = 5;
module_param(UP_medium_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_medium_accel, "Threshold for UP medium acceleration (default is 5)");

static int UP_medium_events = 3;
module_param(UP_medium_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_medium_events, "Number of UP events sent for medium acceleration (default is 3)");

static int UP_fast_accel = 10;
module_param(UP_fast_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_fast_accel, "Threshold for UP fast acceleration (default is 5)");

static int UP_fast_events = 7;
module_param(UP_fast_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(UP_fast_events, "Number of UP events sent for fast acceleration (default is 7)");

static int DOWN_normal_accel = 2;
module_param(DOWN_normal_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_normal_accel, "Trackball minimum number of events to adjust sensitivity (default is 2)");

static int DOWN_normal_events = 1;
module_param(DOWN_normal_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_normal_events, "Number of DOWN events sent for normal acceleration (default is 1)");

static int DOWN_medium_accel = 5;
module_param(DOWN_medium_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_medium_accel, "Threshold for DOWN medium acceleration (default is 5)");

static int DOWN_medium_events = 3;
module_param(DOWN_medium_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_medium_events, "Number of DOWN events sent for medium acceleration (default is 3)");

static int DOWN_fast_accel = 10;
module_param(DOWN_fast_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_fast_accel, "Threshold for DOWN fast acceleration (default is 5)");

static int DOWN_fast_events = 7;
module_param(DOWN_fast_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(DOWN_fast_events, "Number of DOWN events sent for fast acceleration (default is 7)");

static int LEFT_normal_accel = 2;
module_param(LEFT_normal_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_normal_accel, "Trackball minimum number of events to adjust sensitivity (default is 2)");

static int LEFT_normal_events = 1;
module_param(LEFT_normal_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_normal_events, "Number of LEFT events sent for normal acceleration (default is 1)");

static int LEFT_medium_accel = 5;
module_param(LEFT_medium_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_medium_accel, "Threshold for LEFT medium acceleration (default is 5)");

static int LEFT_medium_events = 1;
module_param(LEFT_medium_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_medium_events, "Number of LEFT events sent for medium acceleration (default is 1)");

static int LEFT_fast_accel = 10;
module_param(LEFT_fast_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_fast_accel, "Threshold for LEFT fast acceleration (default is 5)");

static int LEFT_fast_events = 1;
module_param(LEFT_fast_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(LEFT_fast_events, "Number of LEFT events sent for fast acceleration (default is 1)");

static int RIGHT_normal_accel = 2;
module_param(RIGHT_normal_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_normal_accel, "Trackball minimum number of events to adjust sensitivity (default is 2)");

static int RIGHT_normal_events = 1;
module_param(RIGHT_normal_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_normal_events, "Number of RIGHT events sent for normal acceleration (default is 1)");

static int RIGHT_medium_accel = 5;
module_param(RIGHT_medium_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_medium_accel, "Threshold for RIGHT medium acceleration (default is 5)");

static int RIGHT_medium_events = 1;
module_param(RIGHT_medium_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_medium_events, "Number of RIGHT events sent for medium acceleration (default is 1)");

static int RIGHT_fast_accel = 10;
module_param(RIGHT_fast_accel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_fast_accel, "Threshold for RIGHT fast acceleration (default is 5)");

static int RIGHT_fast_events = 1;
module_param(RIGHT_fast_events, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RIGHT_fast_events, "Number of RIGHT events sent for fast acceleration (default is 1)");

#define HOLD_TIMEOUT_DEFAULT 1000
static int HOLD_timeout = HOLD_TIMEOUT_DEFAULT;
module_param(HOLD_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(HOLD_timeout, "Time for SELECT to generate a HOLD event, in msec; MUST be a multiple of 10 (default is 1000)");

#define HOLD_REPEAT_DEFAULT 0
static int HOLD_repeat = HOLD_REPEAT_DEFAULT;
module_param(HOLD_repeat, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(HOLD_repeat, "Number of times to repeat the HOLD event for a continuous SELECT; repeat rate determined by HOLD_timeout (default is 0; ie no repeat, just generate one HOLD event)");

#define RELEASE_DEBOUNCE_TIMEOUT_DEFAULT 100
static int RELEASE_debounce_timeout = RELEASE_DEBOUNCE_TIMEOUT_DEFAULT;
module_param(RELEASE_debounce_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(RELEASE_debounce_timeout, "Time for debouncing the trackball switch after a RELEASE, in msec; MUST be a multiple of 10 (default is 100)");


static int __init trackball_init(void)
{
   int rqstatus = 0;
   int lineread, lastup, lastdown, lastleft, lastright;

   printk("<1>[trackball_init] Starting...\n");

   // Initialize the array that contains the information for the
   // trackball lines of the various hardware platforms
   line_info[MARIO][TRBL_UP]     = MARIO_TRBL_UP_GPIO;
   line_info[MARIO][TRBL_DOWN]   = MARIO_TRBL_DOWN_GPIO;
   line_info[MARIO][TRBL_LEFT]   = MARIO_TRBL_LEFT_GPIO;
   line_info[MARIO][TRBL_RIGHT]  = MARIO_TRBL_RIGHT_GPIO;
   line_info[MARIO][TRBL_SELECT] = MARIO_TRBL_SELECT_GPIO;

   line_info[TURING_V0][TRBL_UP]     = TURING_V0_TRBL_UP_GPIO;
   line_info[TURING_V0][TRBL_DOWN]   = TURING_V0_TRBL_DOWN_GPIO;
   line_info[TURING_V0][TRBL_LEFT]   = TURING_V0_TRBL_LEFT_GPIO;
   line_info[TURING_V0][TRBL_RIGHT]  = TURING_V0_TRBL_RIGHT_GPIO;
   line_info[TURING_V0][TRBL_SELECT] = TURING_V0_TRBL_SELECT_GPIO;

   // Set dinamically the line informarion depending on the version passed
   // as a parameter
   if ((scroll_rev >= 0) && (scroll_rev < MAX_VERSIONS)) {
      trbl_up_gpio     = line_info[scroll_rev][TRBL_UP];
      trbl_down_gpio   = line_info[scroll_rev][TRBL_DOWN];
      trbl_left_gpio   = line_info[scroll_rev][TRBL_LEFT];
      trbl_right_gpio  = line_info[scroll_rev][TRBL_RIGHT];
      trbl_select_gpio = line_info[scroll_rev][TRBL_SELECT];
   }
   else {
      printk("<1>[trackball_init] *** ERROR: scroll_rev=%d is invalid; must be 0-%d\n", scroll_rev, (MAX_VERSIONS-1));
      trbl_up_gpio     = (iomux_pin_name_t) NULL;
      trbl_down_gpio   = (iomux_pin_name_t) NULL;
      trbl_left_gpio   = (iomux_pin_name_t) NULL;
      trbl_right_gpio  = (iomux_pin_name_t) NULL;
      trbl_select_gpio = (iomux_pin_name_t) NULL;
      
      return -1;
   }
   

   // Initialize the trackball interrupts counters (used for calibration)
   trackball_UP_counter = 0;
   trackball_DOWN_counter = 0;
   trackball_LEFT_counter = 0;
   trackball_RIGHT_counter = 0;

   // Initialize the trackball timer structure (used for calibration)
   init_timer(&trackball_timer);  // Add timer to kernel list of timers
   trackball_timer.data = 0;  // Timer currently not running
   trackball_timer.function = trackball_timer_timeout;

   // Initialize the select hold timer structure (used for generating HOLD events)
   init_timer(&hold_timer);  // Add timer to kernel list of timers
   hold_timer.data = 0;  // Timer currently not running
   hold_timer.function = hold_timer_timeout;

   // Initialize the release debounce timer structure (used for debouncing the pushbutton)
   init_timer(&release_debounce_timer);  // Add timer to kernel list of timers
   release_debounce_timer.data = 0;  // Timer currently not running
   release_debounce_timer.function = release_timer_timeout;

   // Set IRQ for trackball GPIO lines; trigger on falling edges
   // Set the trackball GPIO lines as inputs with falling-edge detection,
   // and register the IRQ service routines
   if (trbl_up_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(TRBL_UP_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(TRBL_UP_IRQ, (irq_handler_t) trackball_irq_UP, 0, "trackball", NULL);
      if (rqstatus != 0) {
         printk("<1>[trackball_init] *** ERROR: UP IRQ line = 0x%X; request status = %d\n", TRBL_UP_IRQ, rqstatus);
      }
      if (mxc_request_iomux(trbl_up_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[trackball_init] *** ERROR: could not obtain GPIO pin for trackball UP line\n");
      }
      else {
          mxc_set_gpio_direction(trbl_up_gpio, 1);
      }
   }
   else {
      printk("<1>[trackball_init] *** NOTE: trackball UP GPIO defined as NULL\n");
   }

   if (trbl_down_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(TRBL_DOWN_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(TRBL_DOWN_IRQ, (irq_handler_t) trackball_irq_DOWN, 0, "trackball", NULL);
      if (rqstatus != 0) {
         printk("<1>[trackball_init] *** ERROR: DOWN IRQ line = %d; request status = %d\n", TRBL_DOWN_IRQ, rqstatus);
      }
      if (mxc_request_iomux(trbl_down_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[trackball_init] *** ERROR: could not obtain GPIO pin for trackball DOWN line\n");
      }
      else {
          mxc_set_gpio_direction(trbl_down_gpio, 1);
      }
   }
   else {
      printk("<1>[trackball_init] *** NOTE: trackball DOWN GPIO defined as NULL\n");
   }

   if (trbl_left_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(TRBL_LEFT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(TRBL_LEFT_IRQ, (irq_handler_t) trackball_irq_LEFT, 0, "trackball", NULL);
      if (rqstatus != 0) {
         printk("<1>[trackball_init] *** ERROR: LEFT IRQ line = %d; request status = %d\n", TRBL_LEFT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(trbl_left_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[trackball_init] *** ERROR: could not obtain GPIO pin for trackball LEFT line\n");
      }
      else {
          mxc_set_gpio_direction(trbl_left_gpio, 1);
      }
   }
   else {
      printk("<1>[trackball_init] *** NOTE: trackball LEFT GPIO defined as NULL\n");
   }

   if (trbl_right_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(TRBL_RIGHT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(TRBL_RIGHT_IRQ, (irq_handler_t) trackball_irq_RIGHT, 0, "trackball", NULL);
      if (rqstatus != 0) {
         printk("<1>[trackball_init] *** ERROR: RIGHT IRQ line = %d; request status = %d\n", TRBL_RIGHT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(trbl_right_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[trackball_init] *** ERROR: could not obtain GPIO pin for trackball RIGHT line\n");
      }
      else {
          mxc_set_gpio_direction(trbl_right_gpio, 1);
      }
   }
   else {
      printk("<1>[trackball_init] *** NOTE: trackball RIGHT GPIO defined as NULL\n");
   }

   if (trbl_select_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(TRBL_SELECT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(TRBL_SELECT_IRQ, (irq_handler_t) trackball_irq_SELECT, 0, "trackball", NULL);
      if (rqstatus != 0) {
         printk("<1>[trackball_init] *** ERROR: SELECT IRQ line = %d; request status = %d\n", TRBL_SELECT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(trbl_select_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[trackball_init] *** ERROR: could not obtain GPIO pin for trackball SELECT line\n");
      }
      else {
          mxc_set_gpio_direction(trbl_select_gpio, 1);
      }
   }
   else {
      printk("<1>[trackball_init] *** NOTE: trackball SELECT GPIO defined as NULL\n");
   }
   
   if (debug_trackball > 1) {
      printk("<1>[trackball_init] *** Test trackball; push to end test. ***\n");
      rqstatus = 0;
      lastup = mxc_get_gpio_datain(trbl_up_gpio);
      lastdown = mxc_get_gpio_datain(trbl_down_gpio);
      lastleft = mxc_get_gpio_datain(trbl_left_gpio);
      lastright = mxc_get_gpio_datain(trbl_right_gpio);
      while (rqstatus == 0) { 
         lineread = mxc_get_gpio_datain(trbl_up_gpio);
         if (lineread != lastup) {
            printk("<1>                 UP\n");
            lastup = lineread;
         }
         lineread = mxc_get_gpio_datain(trbl_down_gpio);
         if (lineread != lastdown) {
            printk("<1>                 DOWN\n");
            lastdown = lineread;
         }
         lineread = mxc_get_gpio_datain(trbl_left_gpio);
         if (lineread != lastleft) {
            printk("<1>                 LEFT\n");
            lastleft = lineread;
         }
         lineread = mxc_get_gpio_datain(trbl_right_gpio);
         if (lineread != lastright) {
            printk("<1>                 RIGHT\n");
            lastright = lineread;
         }
         if (mxc_get_gpio_datain(trbl_select_gpio) == 0) {
            printk("<1>                 SELECT\n");
            printk("<1>*** End of trackball test ***\n");
            rqstatus = 1;
         }
      }
   }

   printk("<1>[trackball_init] GPIOs and IRQs have been set up\n");

   return 0;

}

static void __exit trackball_exit(void)
{
   // Release GPIO pins and IRQs
   if (trbl_up_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(trbl_up_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(TRBL_UP_IRQ, NULL);
   }
   if (trbl_down_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(trbl_down_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(TRBL_DOWN_IRQ, NULL);
   }
   if (trbl_left_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(trbl_left_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(TRBL_LEFT_IRQ, NULL);
   }
   if (trbl_right_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(trbl_right_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(TRBL_RIGHT_IRQ, NULL);
   }
   if (trbl_select_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(trbl_select_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(TRBL_SELECT_IRQ, NULL);
   }

   // Stop trackball timers if running
   if (trackball_timer.data != 0) {
      del_timer(&trackball_timer);
      trackball_timer.data = 0;
   }
   if (hold_timer.data != 0) {
      del_timer(&hold_timer);
      hold_timer.data = 0;
   }
   if (release_debounce_timer.data != 0) {
      del_timer(&release_debounce_timer);
      release_debounce_timer.data = 0;
   }

   printk("<1>[trackball_exit] IRQs released and timers stopped.  Lehitraot!\n");
}

module_init(trackball_init);
module_exit(trackball_exit);

static void trackball_timer_timeout(unsigned long timer_data)
{
   int UP_event_counter = 0;
   int DOWN_event_counter = 0;
   int LEFT_event_counter = 0;
   int RIGHT_event_counter = 0;

   if (debug_trackball > 1) {
      printk("<1>[trackball_timer_timeout] trackball_UP_counter = %d\n", trackball_UP_counter);
      printk("<1>[trackball_timer_timeout] trackball_DOWN_counter = %d\n", trackball_DOWN_counter);
      printk("<1>[trackball_timer_timeout] trackball_LEFT_counter = %d\n", trackball_LEFT_counter);
      printk("<1>[trackball_timer_timeout] trackball_RIGHT_counter = %d\n", trackball_RIGHT_counter);
   }

   // Send the appropriate trackball events, depending on the counters
   if ( ((trackball_UP_counter * 100) > (angle_percent * (trackball_UP_counter + trackball_RIGHT_counter))) &&
        ((trackball_UP_counter * 100) > (angle_percent * (trackball_UP_counter + trackball_LEFT_counter))) ) {
      if (trackball_UP_counter < UP_normal_accel) {
         // Didn't cross the minimum threshold, so send no events
         UP_event_counter = 0;
      }
      else if (trackball_UP_counter < UP_medium_accel) {
         // No acceleration
         UP_event_counter = UP_normal_events;
      }
      else if (trackball_UP_counter < UP_fast_accel) {
         // Medium acceleration
         UP_event_counter = UP_medium_events;
      }
      else {
         // Fast acceleration
         UP_event_counter = UP_fast_events;
      }
      if ((debug_trackball != 0) && (UP_event_counter > 0)) {
         printk("<1>[trackball_timer_timeout] Sending TRACKBALL_EVENTCODE_UP %d times\n", UP_event_counter);
      }
      while (UP_event_counter > 0) {
         handle_fiona_event(TRACKBALL_EVENTCODE_UP);
         UP_event_counter--;
      }
   }
   if ( ((trackball_DOWN_counter * 100) > (angle_percent * (trackball_DOWN_counter + trackball_RIGHT_counter))) &&
        ((trackball_DOWN_counter * 100) > (angle_percent * (trackball_DOWN_counter + trackball_LEFT_counter))) ) {
      if (trackball_DOWN_counter < DOWN_normal_accel) {
         // Didn't cross the minimum threshold, so send no events
         DOWN_event_counter = 0;
      }
      else if (trackball_DOWN_counter < DOWN_medium_accel) {
         // No acceleration
         DOWN_event_counter = DOWN_normal_events;
      }
      else if (trackball_DOWN_counter < DOWN_fast_accel) {
         // Medium acceleration
         DOWN_event_counter = DOWN_medium_events;
      }
      else {
         // Fast acceleration
         DOWN_event_counter = DOWN_fast_events;
      }
      if ((debug_trackball != 0) && (DOWN_event_counter > 0)) {
         printk("<1>[trackball_timer_timeout] Sending TRACKBALL_EVENTCODE_DOWN %d times\n", DOWN_event_counter);
      }
      while (DOWN_event_counter > 0) {
         handle_fiona_event(TRACKBALL_EVENTCODE_DOWN);
         DOWN_event_counter--;
      }
   }
   if ( ((trackball_LEFT_counter * 100) > (angle_percent * (trackball_LEFT_counter + trackball_UP_counter))) &&
        ((trackball_LEFT_counter * 100) > (angle_percent * (trackball_LEFT_counter + trackball_DOWN_counter))) ) {
      if (trackball_LEFT_counter < LEFT_normal_accel) {
         // Didn't cross the minimum threshold, so send no events
         LEFT_event_counter = 0;
      }
      else if (trackball_LEFT_counter < LEFT_medium_accel) {
         // No acceleration
         LEFT_event_counter = LEFT_normal_events;
      }
      else if (trackball_LEFT_counter < LEFT_fast_accel) {
         // Medium acceleration
         LEFT_event_counter = LEFT_medium_events;
      }
      else {
         // Fast acceleration
         LEFT_event_counter = LEFT_fast_events;
      }
      if ((debug_trackball != 0) && (LEFT_event_counter > 0)) {
         printk("<1>[trackball_timer_timeout] Sending TRACKBALL_EVENTCODE_LEFT %d times\n", LEFT_event_counter);
      }
      while (LEFT_event_counter > 0) {
         handle_fiona_event(TRACKBALL_EVENTCODE_LEFT);
         LEFT_event_counter--;
      }
   }
   if ( ((trackball_RIGHT_counter * 100) > (angle_percent * (trackball_RIGHT_counter + trackball_UP_counter))) &&
        ((trackball_RIGHT_counter * 100) > (angle_percent * (trackball_RIGHT_counter + trackball_DOWN_counter))) ) {
      if (trackball_RIGHT_counter < RIGHT_normal_accel) {
         // Didn't cross the minimum threshold, so send no events
         RIGHT_event_counter = 0;
      }
      else if (trackball_RIGHT_counter < RIGHT_medium_accel) {
         // No acceleration
         RIGHT_event_counter = RIGHT_normal_events;
      }
      else if (trackball_RIGHT_counter < RIGHT_fast_accel) {
         // Medium acceleration
         RIGHT_event_counter = RIGHT_medium_events;
      }
      else {
         // Fast acceleration
         RIGHT_event_counter = RIGHT_fast_events;
      }
      if ((debug_trackball != 0) && (RIGHT_event_counter > 0)) {
         printk("<1>[trackball_timer_timeout] Sending TRACKBALL_EVENTCODE_RIGHT %d times\n", RIGHT_event_counter);
      }
      while (RIGHT_event_counter > 0) {
         handle_fiona_event(TRACKBALL_EVENTCODE_RIGHT);
         RIGHT_event_counter--;
      }
   }


   // Clear counters and indicate that timer is not running
   trackball_UP_counter = 0;
   trackball_DOWN_counter = 0;
   trackball_LEFT_counter = 0;
   trackball_RIGHT_counter = 0;
   del_timer(&trackball_timer);
   trackball_timer.data = 0;
}

static irqreturn_t trackball_irq_UP (int irq, void *data, struct pt_regs *r)
{
   trackball_UP_counter++;
   if (debug_trackball != 0) {
      printk("<1> [trackball_irq_UP] Got trackball UP signal (count = %d)\n", trackball_UP_counter);
   }
   if (trackball_timer.data == 0) {
      // Timer is not running; start it.
      if (debug_trackball > 1) {
         printk("<1> [trackball_irq_UP] Starting trackball_timer...\n");
      }
      // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
      trackball_timer.expires = jiffies + ((UP_sample_window * HZ) / 1000);
      del_timer(&trackball_timer);
      add_timer(&trackball_timer);
      trackball_timer.data = 1; // Indicate that timer is running
   }
   return IRQ_HANDLED;
}

static irqreturn_t trackball_irq_DOWN (int irq, void *data, struct pt_regs *r)
{
   trackball_DOWN_counter++;
   if (debug_trackball != 0) {
      printk("<1> [trackball_irq_DOWN] Got trackball DOWN signal (count = %d)\n", trackball_DOWN_counter);
   }
   if (trackball_timer.data == 0) {
      // Timer is not running; start it.
      if (debug_trackball > 1) {
         printk("<1> [trackball_irq_DOWN] Starting trackball_timer...\n");
      }
      // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
      trackball_timer.expires = jiffies + ((DOWN_sample_window * HZ) / 1000);
      del_timer(&trackball_timer);
      add_timer(&trackball_timer);
      trackball_timer.data = 1; // Indicate that timer is running
   }
   return IRQ_HANDLED;
}

static irqreturn_t trackball_irq_LEFT (int irq, void *data, struct pt_regs *r)
{
   trackball_LEFT_counter++;
   if (debug_trackball != 0) {
      printk("<1> [trackball_irq_LEFT] Got trackball LEFT signal (count = %d)\n", trackball_LEFT_counter);
   }
   if (trackball_timer.data == 0) {
      // Timer is not running; start it.
      if (debug_trackball > 1) {
         printk("<1> [trackball_irq_LEFT] Starting trackball_timer...\n");
      }
      // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
      trackball_timer.expires = jiffies + ((LEFT_sample_window * HZ) / 1000);
      del_timer(&trackball_timer);
      add_timer(&trackball_timer);
      trackball_timer.data = 1; // Indicate that timer is running
   }
   return IRQ_HANDLED;
}

static irqreturn_t trackball_irq_RIGHT (int irq, void *data, struct pt_regs *r)
{
   trackball_RIGHT_counter++;
   if (debug_trackball != 0) {
      printk("<1> [trackball_irq_RIGHT] Got trackball RIGHT signal (count = %d)\n", trackball_RIGHT_counter);
   }
   if (trackball_timer.data == 0) {
      // Timer is not running; start it.
      if (debug_trackball > 1) {
         printk("<1> [trackball_irq_RIGHT] Starting trackball_timer...\n");
      }
      // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
      trackball_timer.expires = jiffies + ((RIGHT_sample_window * HZ) / 1000);
      del_timer(&trackball_timer);
      add_timer(&trackball_timer);
      trackball_timer.data = 1; // Indicate that timer is running
   }
   return IRQ_HANDLED;
}


static void hold_timer_timeout(unsigned long timer_data)
{
   if (debug_trackball != 0) {
      printk("<1> [hold_timer_timeout] Got trackball HOLD timeout\n");
      printk("<1> [hold_timer_timeout] Sending TRACKBALL_EVENTCODE_HELD\n");
   }
   // Send HOLD event
   handle_fiona_event(TRACKBALL_EVENTCODE_HELD);
   // Indicate that timer is not running
   del_timer(&hold_timer);
   hold_timer.data = 0;
}


static void release_timer_timeout(unsigned long timer_data)
{
   if (debug_trackball != 0) {
      printk("<1> [release_timer_timeout] Debounce period expired\n");
   }
   // Indicate that timer is not running
   del_timer(&release_debounce_timer);
   release_debounce_timer.data = 0;
}


static irqreturn_t trackball_irq_SELECT (int irq, void *data, struct pt_regs *r)
{
   // If the debounce timer is running, ignore the SELECT or RELEASE signal
   if (release_debounce_timer.data != 0) {
      if (debug_trackball != 0) {
         printk("<1> [trackball_irq_SELECT] Ignoring SELECT/RELEASE signal while debouncing switch\n");
      }
      return IRQ_HANDLED;
   }

   if (mxc_get_gpio_datain(trbl_select_gpio) == 0) {
      if (debug_trackball != 0) {
         printk("<1> [trackball_irq_SELECT] Got trackball SELECT signal\n");
      }
      if (hold_timer.data == 0) {
         // Timer is not running; start it.
         if (debug_trackball > 1) {
            printk("<1> [trackball_irq_SELECT] Starting hold_timer...\n");
         }
         // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
         hold_timer.expires = jiffies + ((HOLD_timeout * HZ) / 1000);
         del_timer(&hold_timer);
         add_timer(&hold_timer);
         hold_timer.data = 1; // Indicate that timer is running
      }
      if (debug_trackball != 0) {
         printk("<1> [trackball_irq_SELECT] Sending TRACKBALL_EVENTCODE_PRESSED\n");
      }
      handle_fiona_event(TRACKBALL_EVENTCODE_PRESSED);
      
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(TRBL_SELECT_IRQ, IRQF_TRIGGER_RISING);
   }
   else {
      // SELECT line is released
      if (debug_trackball != 0) {
         printk("<1> [trackball_irq_SELECT] Got trackball RELEASE signal\n");
      }
      // Stop trackball HOLD timer since the user has released the trackball, if its running
      if (hold_timer.data != 0) {
         del_timer(&hold_timer);
         hold_timer.data = 0;  // Indicate that the timer is no longer running
         if (debug_trackball > 0) {
            printk("<1> [trackball_irq_SELECT] Deleted hold_timer...\n");
         }
      }

      // Start the RELEASE debounce timer, to avoid spurious signals
      if (debug_trackball > 1) {
         printk("<1> [trackball_irq_SELECT] Starting release_debounce_timer...\n");
      }
      // Convert from ms to jiffies; in Fiona HZ=100, so each jiffy is 10ms
      release_debounce_timer.expires = jiffies + ((RELEASE_debounce_timeout * HZ) / 1000);
      del_timer(&release_debounce_timer);
      add_timer(&release_debounce_timer);
      release_debounce_timer.data = 1; // Indicate that timer is running

      if (debug_trackball != 0) {
         printk("<1> [trackball_irq_SELECT] Sending TRACKBALL_EVENTCODE_RELEASED\n");
      }
      handle_fiona_event((TRACKBALL_EVENTCODE_PRESSED)|0x80);

      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(TRBL_SELECT_IRQ, IRQF_TRIGGER_FALLING);
   }
   return IRQ_HANDLED;
}

MODULE_AUTHOR("Lab126");
MODULE_LICENSE("GPL");
