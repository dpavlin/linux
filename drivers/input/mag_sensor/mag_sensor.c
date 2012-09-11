/*
** Magnetic sensor driver routine for Mario.  (C) 2008 Lab126
*/


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
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
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
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
#include <asm/arch-mxc/pmic_power.h>
#include <asm/arch/gpio.h>

#include "mag_sensor.h"

typedef u32 * volatile reg_addr_t;

// Device structure
static struct miscdevice mag_sensor_dev = { MISC_DYNAMIC_MINOR,
                                            "mag_sensor",
                                            NULL,
                                          };

// Global variables

static struct timer_list debounce_timer;
static char line_state = MAG_SENSOR_START;

// Proc entry structure and global variable.
#define MAG_SENSOR_PROC_FILE "mag_sensor"
#define PROC_CMD_LEN 50
static struct proc_dir_entry *proc_entry;
static int mag_sensor_disable = 0;  // 0=enabled; non-zero=disabled

// Variable for magnetic sensor input gpio line; depends on HW platform
static iomux_pin_name_t mag_sensor_gpio;


// Module parameters

#define HW_REV_DEFAULT 1
static int hw_rev = HW_REV_DEFAULT;
module_param(hw_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hw_rev, "Magnetic Sensor hardware revision; 0=Turing EVT1 and Nell Prototype, 1=Turing EVT1.5 and later, or Nell (default is 1)");

#define DEBOUNCE_TIMEOUT_DEFAULT 1000
static int debounce_timeout = DEBOUNCE_TIMEOUT_DEFAULT;
module_param(debounce_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debounce_timeout, "Time for debouncing the mag_sensor input, in msec; MUST be a multiple of 10 (default is 1000)");

static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");


/*
 * Report the mag_sensor uevent for any processes listening in the userspace
 */
static void report_event(char *value)
{
   // Send uevent so userspace power daemon can monitor mag_sensor activity

   char *envp[] = {value, "DRIVER=mag_sensor", NULL};

   if (debug > 0) {
      printk("<1> [report_event] Sending uevent: %s\n", value);
   }
   // Send uevent to user space
   if( kobject_uevent_env(&mag_sensor_dev.this_device->kobj, KOBJ_CHANGE, envp) )
   {
      printk( "<1>ERROR: mag_sensor failed to send uevent\n" );
   }
}

static void debounce_timer_timeout(unsigned long timer_data)
{
   if (debug > 1) {
      printk("<1> [debounce_timer_timeout] Debounce period expired\n");
   }
   // Send appropriate event, depending on the state of the mag_sensor line
   if (( mxc_get_gpio_datain(mag_sensor_gpio) == MAG_SENSOR_ON ) &&
       ( line_state != MAG_SENSOR_ON )) {
      if (debug != 0) {
         printk("<1> [debounce_timer_timeout] Got mag_sensor ON signal; sending event\n");
      }
      report_event(MAG_STATE_CLOSED);
      // Next time around we want to trigger on RISING-EDGE IRQ
      set_irq_type(MAG_SENSOR_IRQ, IRQF_TRIGGER_RISING);
      line_state = MAG_SENSOR_ON;
   }

   // Check if the mag_sensor line is OFF
   if (( mxc_get_gpio_datain(mag_sensor_gpio) == MAG_SENSOR_OFF ) &&
       ( line_state != MAG_SENSOR_OFF )) {
      if (debug != 0) {
         printk("<1> [debounce_timer_timeout] Got mag_sensor OFF signal\n");
      }
      report_event(MAG_STATE_OPEN);
      line_state = MAG_SENSOR_OFF;
      // Next time around we want to trigger on FALLING-EDGE IRQ
      set_irq_type(MAG_SENSOR_IRQ, IRQF_TRIGGER_FALLING);
   }

   // Indicate that the debounce timer is not running
   del_timer(&debounce_timer);
   debounce_timer.data = 0;
}


static irqreturn_t mag_sensor_irq (int irq, void *data, struct pt_regs *r)
{
   // If the debounce timer is running, ignore the sensor signal
   if (debounce_timer.data != 0) {
      if (debug != 0) {
         printk("<1> [mag_sensor_irq] Ignoring Magnetic Sensor signal while debouncing switch\n");
      }
      return IRQ_HANDLED;
   }

   // Start the debounce timer, to avoid spurious signals
   if (debug > 1) {
      printk("<1> [mag_sensor_irq_UP] Starting debounce_timer...\n");
   }
   // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
   del_timer(&debounce_timer);
   debounce_timer.expires = jiffies + ((debounce_timeout * HZ) / 1000);
   add_timer(&debounce_timer);
   debounce_timer.data = 1; // Indicate that timer is running

   return IRQ_HANDLED;
}


/*!
 * This function puts the Magnetic Sensor driver in low-power mode/state,
 * by disabling interrupts from the input line.
 */
void mag_sensor_off(void)
{
   if (debug != 0) {
      printk("<1> [mag_sensor_off] Stopping timers and disabling IRQ...\n");
   }

   // Stop mag_sensor timer if running
   if (debounce_timer.data != 0) {
      del_timer(&debounce_timer);
      debounce_timer.data = 0;
   }

   // Disable IRQ line
   if (mag_sensor_gpio != (iomux_pin_name_t)NULL) {
      disable_irq(MAG_SENSOR_IRQ);
   }
}


/*!
 * This function puts the Magnetic Sensor driver in low-power mode/state,
 * by disabling interrupts from the input line.
 * @param   pdev  the device structure used to give information on 5-Way
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function always returns 0.
 */
static int mag_sensor_suspend(struct platform_device *pdev, pm_message_t state)
{
   mag_sensor_off();

   return 0;
}


/*!
 * This function brings the 5-Way driver back from low-power state,
 * by re-enabling the line interrupts.
 *
 * @return  The function always returns 0.
 */
void mag_sensor_on(void)
{
   if (debug != 0) {
      printk("<1> [mag_sensor_on] Enabling IRQ...\n");
   }

   // Enable the IRQ
   if (mag_sensor_gpio != (iomux_pin_name_t)NULL) {
      enable_irq(MAG_SENSOR_IRQ);
   }
}


/*!
 * This function brings the Magnetic Sensor driver back from low-power state,
 * by re-enabling the line interrupts.
 *
 * @param   pdev  the device structure used to give information on Keypad
 *                to resume
 *
 * @return  The function always returns 0.
 */
static int mag_sensor_resume(struct platform_device *pdev)
{
   mag_sensor_on();

   return 0;
}


/*
 * This function returns the current state of the mag_sensor device
 */
int mag_sensor_proc_read( char *page, char **start, off_t off,
                      int count, int *eof, void *data )
{
   int len;

   if (off > 0) {
     *eof = 1;
     return 0;
   }
   if (mag_sensor_disable == 0) {
      len = sprintf(page, "mag_sensor is enabled\n");
   }
   else {
      len = sprintf(page, "mag_sensor is disabled\n");
   }
   return len;
}


/*
 * This function executes mag_sensor control commands based on the input string.
 *    enable  = enable the mag_sensor
 *    disable = disable the mag_sensor
 */
ssize_t mag_sensor_proc_write( struct file *filp, const char __user *buff,
                            unsigned long len, void *data )

{
   char command[PROC_CMD_LEN];

   if (len > PROC_CMD_LEN) {
      printk(KERN_ERR "mag_sensor: command is too long!\n");
      return -ENOSPC;
   }

   if (copy_from_user(command, buff, len)) {
      return -EFAULT;
   }

   if ( !strncmp(command, "enable", 6) ) {
      // Requested to enable mag_sensor
      if (mag_sensor_disable == 0) {
         // Magnetic sensor was already enabled; do nothing
         printk(KERN_INFO "The mag_sensor device was already enabled!\n");
      }
      else {
         // Enable mag_sensor
         mag_sensor_on();
         mag_sensor_disable = 0;
         printk(KERN_INFO "The mag_sensor device is now enabled\n");
      }
   }
   else if ( !strncmp(command, "disable", 7) ) {
      // Requested to disable mag_sensor
      if (mag_sensor_disable != 0) {
         // Magnetic sensor was already disabled; do nothing
         printk(KERN_INFO "The mag_sensor device was already disabled!\n");
      }
      else {
         // Disable mag_sensor
         mag_sensor_off();
         mag_sensor_disable = 1;
         printk(KERN_INFO "The mag_sensor device is now disabled\n");
      }
   }
   else {
      printk(KERN_ERR "ERROR: Unrecognized mag_sensor command\n");
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
static int __devinit mag_sensor_probe(struct platform_device *pdev)
{
   int rqstatus = 0;
   int error;

   printk("<1>[mag_sensor_probe] Starting...\n");

   /* Create proc entry */
   proc_entry = create_proc_entry(MAG_SENSOR_PROC_FILE, 0644, NULL);
   if (proc_entry == NULL) {
      printk(KERN_INFO "mag_sensor: Couldn't create proc entry\n");
      return -ENOMEM;
   } else {
      proc_entry->read_proc = mag_sensor_proc_read;
      proc_entry->write_proc = mag_sensor_proc_write;
      proc_entry->owner = THIS_MODULE;
   }


   // Initialize the variable that contains the information for the
   // magnetic sensor input line, of the various hardware platforms
   switch (hw_rev) {
   case TURING_EVT1:
      mag_sensor_gpio = TURING_EVT1_MAG_SENSOR_GPIO;
      break;
   case TURING_NELL:
      mag_sensor_gpio = TURING_NELL_MAG_SENSOR_GPIO;
      break;
   default:
      printk("<1>[mag_sensor_init] *** ERROR: hw_rev=%d is invalid; must be 0-%d\n", hw_rev, (MAX_VERSIONS-1));
      mag_sensor_gpio = (iomux_pin_name_t) NULL;
      return -1;
      break;
   }

   // Initialize the debounce timer structure
   init_timer(&debounce_timer);  // Add timer to kernel list of timers
   debounce_timer.data = 0;  // Timer currently not running
   debounce_timer.function = debounce_timer_timeout;

   // Set the mag_sensor GPIO line as input with falling-edge detection,
   // and register the IRQ service routines
   if (mag_sensor_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(MAG_SENSOR_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(MAG_SENSOR_IRQ, (irq_handler_t) mag_sensor_irq, 0, "mag_sensor", NULL);
      if (rqstatus != 0) {
         printk("<1>[mag_sensor_probe] *** ERROR: MAG_SENSOR IRQ line = 0x%X; request status = %d\n", MAG_SENSOR_IRQ, rqstatus);
      }
      if (mxc_request_iomux(mag_sensor_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[mag_sensor_probe] *** ERROR: could not obtain GPIO pin for mag_sensor line\n");
      }
      else {
          mxc_set_gpio_direction(mag_sensor_gpio, 1);
      }
   }
   else {
      printk("<1>[mag_sensor_probe] *** NOTE: mag_sensor GPIO defined as NULL\n");
   }

   printk("<1>[mag_sensor_probe] GPIO and IRQ have been set up\n");

   // Register the device file
   error = misc_register(&mag_sensor_dev);
   if (error) {
      printk(KERN_ERR "mag_sensor_dev: failed to register device\n");
      return error;
   }
   
   return 0;
}


/*!
 * Dissociates the driver from the mag_sensor device.
 *
 * @param   pdev  the device structure used to give information on which SDHC
 *                to remove
 *
 * @return  The function always returns 0.
 */
static int __devexit mag_sensor_remove(struct platform_device *pdev)
{
   // Release IRQ and GPIO pin
   if (mag_sensor_gpio != (iomux_pin_name_t)NULL) {
      free_irq(MAG_SENSOR_IRQ, NULL);
      mxc_free_iomux(mag_sensor_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }

   // Stop mag_sensor timer if running
   if (debounce_timer.data != 0) {
      del_timer(&debounce_timer);
      debounce_timer.data = 0;
   }

   remove_proc_entry(MAG_SENSOR_PROC_FILE, &proc_root);
   misc_deregister(&mag_sensor_dev);

   printk("<1>[mag_sensor_exit] IRQ released and timer stopped.\n");

   return 0;
}


/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mag_sensor_driver = {
        .driver = {
                   .name = "mag_sensor",
                   .owner  = THIS_MODULE,
                   },
        .suspend = mag_sensor_suspend,
        .resume = mag_sensor_resume,
        .probe = mag_sensor_probe,
        .remove = __devexit_p(mag_sensor_remove),
};

/*!
 * This function is called for module initialization.
 * It registers the mag_sensor char driver.
 *
 * @return      0 on success and a non-zero value on failure.
 */
static int __init mag_sensor_init(void)
{
        platform_driver_register(&mag_sensor_driver);
        mag_sensor_probe(NULL);
        printk(KERN_INFO "Magnetic sensor driver loaded\n");
        return 0;
}

/*!
 * This function is called whenever the module is removed from the kernel. It
 * unregisters the mag_sensor driver from kernel.
 */
static void __exit mag_sensor_exit(void)
{
        mag_sensor_remove(NULL);
        platform_driver_unregister(&mag_sensor_driver);
}



module_init(mag_sensor_init);
module_exit(mag_sensor_exit);

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("Magnetic Sensor Device Driver");
MODULE_LICENSE("GPL");
