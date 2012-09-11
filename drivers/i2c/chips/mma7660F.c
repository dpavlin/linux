/*
** Freescale MMA7660F Accelerometer driver routine for Mario.  (C) 2008 Lab126
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
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
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
#include <asm/arch/gpio.h>

#include <linux/i2c.h>
#include <asm/arch-mxc/mxc_i2c.h>

//Logging
unsigned int logging_mask;
#define LLOG_G_LOG_MASK logging_mask
#define LLOG_KERNEL_COMP "accelerometer"
#include <llog.h>

#include "mma7660F.h"

typedef u32 * volatile reg_addr_t;

// Device structure
static struct miscdevice mma7660F_dev = { MISC_DYNAMIC_MINOR,
                                            "mma7660F",
                                            NULL,
                                          };

// Global variables

static char device_orientation = DEVICE_INIT_ORIENTATION;
static int mma7660F_reads_OK = 0;

// Proc entry structure
#define ACCEL_PROC_FILE "accelerometer"
#define PROC_CMD_LEN 50
static struct proc_dir_entry *proc_entry;

// Workqueue
static struct delayed_work mma7660F_tq_d;
static void detect_mma7660F_pos(struct work_struct *);

// The following array contains information for the MMA7660F Accelerometer
// lines for each different hardware revision.
// A NULL value in any element indicates that the line is not a GPIO;
// it may be unused, or it may be not connected.  In this case, we will not
// be able to take interrupts from such a line.
static iomux_pin_name_t line_info[MAX_VERSIONS];

static iomux_pin_name_t int_gpio;

// i2c-related variables
static int mma7660F_attach(struct i2c_adapter *adapter);
static int mma7660F_detach(struct i2c_client *client);


/*!
 * This structure is used to register the device with the i2c driver
 */
static struct i2c_driver mma7660F_i2c_driver = {
        .driver = {
                   .owner = THIS_MODULE,
                   .name = "Freescale MMA7660F Accelerometer Client",
                   },
        .attach_adapter = mma7660F_attach,
        .detach_client = mma7660F_detach,
};

/*!
 * This structure is used to register the client with the i2c driver
 */
static struct i2c_client mma7660F_i2c_client = {
        .name = "Accel I2C dev",
        .addr = ACCEL_I2C_ADDRESS,
        .driver = &mma7660F_i2c_driver,
};


// Module parameters

#define HW_REV_DEFAULT 1
static int hw_rev = HW_REV_DEFAULT;
module_param(hw_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hw_rev, "System hardware revision; 0=Mario, 1=Nell (default is 1)");

#define ROTATION_DEFAULT 0
static int rotation = ROTATION_DEFAULT;
module_param(rotation, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rotation, "Rotation, in degrees, to redefine which way is 'up' (default is 0 and all values will be rounded to the nearest multiple of 90, and mod 360)");

#define FLIP_DEFAULT 0
static int flip = FLIP_DEFAULT;
module_param(flip, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(flip, "Change the definition of face_up and face_down; 0 = face_up is with IC mounted on top of motherboard; any other value = with IC mounted on bottom of motherboard (default is 0)");

static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");

// Sampling constants and globals
#define DEFAULT_ACCEL_SAMPLING_RATE    SR_AMSR_AM8
static unsigned char accel_sampling_rate = DEFAULT_ACCEL_SAMPLING_RATE;
#define DEFAULT_ACCEL_TILT_DEBOUNCE    SR_FILT_6
static unsigned char accel_tilt_debounce = DEFAULT_ACCEL_TILT_DEBOUNCE;

// Shake and Tap constants and globals
//#define DEFAULT_ACCEL_PULSE_THRESHOLD  0x0B
#define DEFAULT_ACCEL_PULSE_THRESHOLD  0x09
#define DEFAULT_ACCEL_PULSE_DEBOUNCE   0x07
static int shake_enabled = 0;   // Default is disabled
static int shake_x_enabled = 0; // Default is disabled; use proc entry to enable
static int shake_y_enabled = 0; // Default is disabled; use proc entry to enable
static int shake_z_enabled = 0; // Default is disabled; use proc entry to enable
static int tap_enabled = 0;     // Default is disabled
static int tap_x_enabled = 0;   // Default is disabled; use proc entry to enable
static int tap_y_enabled = 0;   // Default is disabled; use proc entry to enable
static int tap_z_enabled = 0;   // Default is disabled; use proc entry to enable

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



/*!
 * Read or Write data to/from the MMA7660F via the i2c interface
 * @param   reg_addr  address of the register to be written
 * @param   wr_data   data to be written
 *
 * @return  The function returns 0 if successful,
 *          or a negative number on failure
 */
static int mma7660F_i2c_client_xfer(unsigned int addr, char *reg,
                                         int reg_len, char *buf, int num,
                                         int tran_flag)
{
        struct i2c_msg msg[2];
        int ret;

        msg[0].addr = addr;
        msg[0].len = reg_len;
        msg[0].buf = reg;
        msg[0].flags = tran_flag;
        msg[0].flags &= ~I2C_M_RD;

        msg[1].addr = addr;
        msg[1].len = num;
        msg[1].buf = buf;
        msg[1].flags = tran_flag;

        if (tran_flag & MXC_I2C_FLAG_READ) {
                msg[1].flags |= I2C_M_RD;
        } else {
                msg[1].flags &= ~I2C_M_RD;
        }

        ret = i2c_transfer(mma7660F_i2c_client.adapter, msg, 2);
        if (ret >= 0)
                return 0;

        return ret;
}



/*!
 * Write a byte to the requested MMA7660F register
 * @param   reg_addr  address of the register to be written
 * @param   wr_data   data to be written
 *
 * @return  The function returns 0 or a positive integer if successful,
 *          or a negative number on failure
 */

static int write_mma7660F_reg(unsigned char reg_addr, char wr_data)
{
   unsigned char mma7660F_reg_addr;
   char mma7660F_wr_data[ACCEL_REG_DATA_LEN];
   int ret_code;

   mma7660F_reg_addr = reg_addr;
   mma7660F_wr_data[0] = wr_data;
   ret_code = mma7660F_i2c_client_xfer(ACCEL_I2C_ADDRESS, &mma7660F_reg_addr,
              ACCEL_REG_ADDR_LEN, mma7660F_wr_data, ACCEL_REG_DATA_LEN, 
              MXC_I2C_FLAG_WRITE);
   LLOG_DEBUG3("Wrote 0x%X to register 0x%X; ret_code = 0x%X\n", mma7660F_wr_data[0], mma7660F_reg_addr, ret_code);
   if (ret_code < 0) {
      LLOG_ERROR("write_err", "", "Attempted writing 0x%X to MMA7660F register 0x%X; failed with ret_code = 0x%X\n", mma7660F_wr_data[0], mma7660F_reg_addr, ret_code);
   }

   return ret_code;
}


/*!
 * Read a byte from the requested MMA7660F register
 * @param   reg_addr  address of the register to be read
 * @param   *rd_buff pointer to register or buffer where data read is returned
 * @param   count    number of bytes to read (if sequential registers read)
 *
 * @return  The function returns 0 or a positive integer if successful,
 *          or a negative number on failure
 *          NOTE: data read is returned in rd_buff
 */

static int read_mma7660F_regs(unsigned char reg_addr, char *rd_buff, int count)
{
   unsigned char mma7660F_reg_addr;
   int ret_code;
   int i;

   mma7660F_reg_addr = reg_addr;
   ret_code = mma7660F_i2c_client_xfer(ACCEL_I2C_ADDRESS, &mma7660F_reg_addr,
              ACCEL_REG_ADDR_LEN, rd_buff, count, MXC_I2C_FLAG_READ);
   if ((ret_code < 0) && (debug > 0)) {
      LLOG_ERROR("read_fail" ,"", "Attempted reading from MMA7660F register 0x%X; failed with ret_code = 0x%X\n", mma7660F_reg_addr, ret_code);
   }
   else {
      // Read successful
      if (debug > 3) {
         for (i=0; i<count; i++) {
            LLOG_DEBUG3("Read 0x%X from register 0x%X; ret_code = 0x%X\n", rd_buff[i], mma7660F_reg_addr+i, ret_code);
         }
      }
   }

   return ret_code;

}


/*
 * Initialize the MMA7660F Accelerometer
 */
void init_mma7660F(void)
{
   unsigned char interrupts = 0;
   unsigned char pulse_det = 0;
   unsigned char sample_rates = 0;

   write_mma7660F_reg(ACCEL_MODE_REG, 0); // Sleep mode so we can modify params

   // Interrupt on any position change detected
   interrupts = INTSU_FBINT | INTSU_PLINT; 

   // Initialize sample rates for orientation detection
   sample_rates = accel_tilt_debounce | accel_sampling_rate; 

   // Interrupt on shake if one or more axes are enabled
   shake_enabled = 0;
   if (shake_x_enabled != 0) {
      interrupts |= INTSU_SHINTX;
      shake_enabled = 1;
   }
   if (shake_y_enabled != 0) {
      interrupts |= INTSU_SHINTY;
      shake_enabled = 1;
   }
   if (shake_z_enabled != 0) {
      interrupts |= INTSU_SHINTZ;
      shake_enabled = 1;
   }

   // Setup tap detection if enabled
   pulse_det = DEFAULT_ACCEL_PULSE_THRESHOLD;
   tap_enabled = 0;
   if (tap_x_enabled != 0) {
      interrupts |= INTSU_PDINT;
      pulse_det &= ~(SR_PDET_XDA);
      tap_enabled = 1;
   }
   else {
      pulse_det |= SR_PDET_XDA;
   }
   if (tap_y_enabled != 0) {
      interrupts |= INTSU_PDINT;
      pulse_det &= ~(SR_PDET_YDA);
      tap_enabled = 1;
   }
   else {
      pulse_det |= SR_PDET_YDA;
   }
   if (tap_z_enabled != 0) {
      interrupts |= INTSU_PDINT;
      pulse_det &= ~(SR_PDET_ZDA);
      tap_enabled = 1;
   }
   else {
      pulse_det |= SR_PDET_ZDA;
   }

   // If tap is enabled, set the correspoding sample rate
   if (tap_enabled != 0) {
      sample_rates &= ~(SR_AMSR);
      sample_rates |= SR_AMSR_AMPD;
   }

   // Write the MMA7660F registers with the setup data
   write_mma7660F_reg(ACCEL_INTSU_REG, interrupts); 
   write_mma7660F_reg(ACCEL_SR_REG, sample_rates); 
   write_mma7660F_reg(ACCEL_PDET_REG, pulse_det); 
   write_mma7660F_reg(ACCEL_PD_REG, DEFAULT_ACCEL_PULSE_DEBOUNCE); 

   // Put device in active mode, Note: INT line is open-drain because we
   // are enabling the GPIO internal pullup resistor
   write_mma7660F_reg(ACCEL_MODE_REG, MODE_ACTIVE); 
}


/*
 * Send a uevent for any processes listening in the userspace
 */
static void report_event(char *value)
{
        char *envp[] = {value, "DRIVER=accelerometer", NULL};
        // Send uevent to user space
        if( kobject_uevent_env(&mma7660F_dev.this_device->kobj, KOBJ_CHANGE, envp) )
        {
           LLOG_ERROR("uevent", "", "mma7660F accelerometer driver failed to send uevent\n" );
        }
}


/*
 * Send orientation through the input channel
 */

static void report_orientation(char orientation)
{
   switch (orientation) {
   case DEVICE_UNKNOWN:
      LLOG_WARN("orient_unknown", "", "Device orientation UNKNOWN\n");
      break;
   case DEVICE_UP:
      report_event(ORIENTATION_STRING_UP);
      break;
   case DEVICE_DOWN:
      report_event(ORIENTATION_STRING_DOWN);
      break;
   case DEVICE_LEFT:
      report_event(ORIENTATION_STRING_LEFT);
      break;
   case DEVICE_RIGHT:
      report_event(ORIENTATION_STRING_RIGHT);
      break;
   case DEVICE_FACE_UP:
      report_event(ORIENTATION_STRING_FACE_UP);
      break;
   case DEVICE_FACE_DOWN:
      report_event(ORIENTATION_STRING_FACE_DOWN);
      break;
   case DEVICE_FREE_FALL:
      report_event(ORIENTATION_STRING_FREE_FALL);
      break;
   default:
      LLOG_WARN("bad_val_orient", "", "INVALID ORIENTATION VALUE PASSED\n");
      break;
   };
   if (debug > 0) {
      switch (orientation) {
      case DEVICE_UNKNOWN:
         LLOG_DEBUG0("Device orientation is now UNKNOWN\n");
         break;
      case DEVICE_UP:
         LLOG_DEBUG0("Device orientation is now UP\n");
         break;
      case DEVICE_DOWN:
         LLOG_DEBUG0("Device orientation is now DOWN\n");
         break;
      case DEVICE_LEFT:
         LLOG_DEBUG0("Device orientation is now LEFT\n");
         break;
      case DEVICE_RIGHT:
         LLOG_DEBUG0("Device orientation is now RIGHT\n");
         break;
      case DEVICE_FACE_UP:
         LLOG_DEBUG0("Device orientation is now FACE_UP\n");
         break;
      case DEVICE_FACE_DOWN:
         LLOG_DEBUG0("Device orientation is now FACE_DOWN\n");
         break;
      case DEVICE_FREE_FALL:
         LLOG_DEBUG0("Device orientation is now FREE_FALL\n");
         break;
      default:
         break;
      };
   }
}


/*
 * Correct the device's position, based on the orientation of the device
 * on the motherboard.
 */

char adjust_pos(char position)
{
   int rotation_index;

   // Calculate a "rotation index" as follows:
   // 0 = angle is within 45 degrees of 0
   // 1 = angle is within 45 degrees of 90
   // 2 = angle is within 45 degrees of 180
   // 3 = angle is within 45 degrees of 270
   // Negative angles are transformed to positive, ie clockwise;
   // for example, -90 is equivalent to 270
   // This index is used to correct X,Y notifications, not Z

   if (rotation >= 0) {
      rotation_index = ((rotation + 45) % 360) / 90;
   }
   else {
      rotation_index = ( (360 - abs(rotation) + 45) %360) / 90;
   }


   // Make any necessary adjustments based on rotation
   switch (rotation_index) {
   case 0:
      // No rotation, so no adjustment necessary
      return position;
      break;
   case 1:
      // Device rotated 90 degrees clockwise
      switch (position) {
      case DEVICE_UP:    return DEVICE_LEFT;
      case DEVICE_RIGHT: return DEVICE_UP;
      case DEVICE_DOWN:  return DEVICE_RIGHT;
      case DEVICE_LEFT:  return DEVICE_DOWN;
      default: return position;
      }
      break;
   case 2:
      // Device rotated 180 degrees
      switch (position) {
      case DEVICE_UP:    return DEVICE_DOWN;
      case DEVICE_RIGHT: return DEVICE_LEFT;
      case DEVICE_DOWN:  return DEVICE_UP;
      case DEVICE_LEFT:  return DEVICE_RIGHT;
      default: return position;
      }
      break;
   case 3:
      // Device rotated 270 degrees
      switch (position) {
      case DEVICE_UP:    return DEVICE_RIGHT;
      case DEVICE_RIGHT: return DEVICE_DOWN;
      case DEVICE_DOWN:  return DEVICE_LEFT;
      case DEVICE_LEFT:  return DEVICE_UP;
      default: return position;
      }
      break;
   default: return position;
   }
   return position;
}


/*
 * Read the relevant registers of the MMA7660F and determine the
 * device's orientation.
 */

unsigned char prev_tilt_status = 0;

static void detect_mma7660F_pos(struct work_struct *work)
{
   signed char tilt_status;
   unsigned char new_tilt_status;
   char position = DEVICE_UNKNOWN;
   int timeout;

   // Read the tilt status register until data is valid or timeout

   tilt_status = TILT_ALERT;
   timeout = 10;  // Number of times to retry reading register
   while ( ((tilt_status & TILT_ALERT) != 0) && (timeout-- > 0)) {
      read_mma7660F_regs(ACCEL_TILT_REG, &tilt_status, 1);
      schedule();  // Relax CPU use
   }
   if ( timeout <= 0) {
      if (debug > 1) {
         LLOG_ERROR("tilt_rd_fail", "", "Could not read MMA7660F Tilt Status register - exiting...\n");
      }
      return;
   }

   // Only consider newly-set tilt status bits
   new_tilt_status = (unsigned char)tilt_status;

   // If tap is enabled and detected, send notification
   if ( (tap_enabled != 0) && ((new_tilt_status & TILT_PULSE) != 0) ) {
      report_event(ORIENTATION_STRING_TAP);
      schedule();
      LLOG_DEBUG1("TRON detected TAP\n");
   }

   // If shake is enabled and detected, send notification
   if ( (shake_enabled != 0) && ((new_tilt_status & TILT_SHAKE) != 0) ) {
      report_event(ORIENTATION_STRING_SHAKE);
      schedule();
      LLOG_DEBUG1("TRON detected SHAKE\n");
   }


   switch (new_tilt_status & TILT_POLA) {
      case TILT_POLA_UP:
         LLOG_DEBUG1("TRON is UP\n");
         break;
      case TILT_POLA_DOWN:
         LLOG_DEBUG1("TRON is DOWN\n");
         break;
      case TILT_POLA_LEFT:
         LLOG_DEBUG1("TRON is LEFT\n");
         break;
      case TILT_POLA_RIGHT:
         LLOG_DEBUG1("TRON is RIGHT\n");
         break;
      default:
         LLOG_DEBUG1("TRON is u/d/l/r UNKNOWN\n");
         break;
   }
   switch (new_tilt_status & TILT_BAFR) {
      case TILT_BAFR_FRONT:
         LLOG_DEBUG1("TRON is FACE DOWN\n");
         break;
      case TILT_BAFR_BACK:
         LLOG_DEBUG1("TRON is FACE UP\n");
         break;
      default:
         LLOG_DEBUG1("TRON is face u/d UNKNOWN\n");
         break;
   }

   if ( ((new_tilt_status ^ prev_tilt_status) & TILT_POLA) == 0) {
      // No change in u/d/l/r orientation
      tilt_status &= ~(TILT_POLA);
   }
   if ( ((new_tilt_status ^ prev_tilt_status) & TILT_BAFR) == 0) {
      // No change in front/back orientation
      tilt_status &= ~(TILT_BAFR);
   }
   prev_tilt_status = new_tilt_status;  // Save for next time

   LLOG_DEBUG1("new_tilt_status = 0x%x\n", new_tilt_status);
   LLOG_DEBUG1("tilt_status = 0x%x\n", tilt_status);

   // Get the device position and adjust for rotation
   if ( (tilt_status & TILT_POLA) == TILT_POLA_UP) {
      position = adjust_pos(DEVICE_UP);
   }
   else if ( (tilt_status & TILT_POLA) == TILT_POLA_DOWN) {
      position = adjust_pos(DEVICE_DOWN);
   }
   else if ( (tilt_status & TILT_POLA) == TILT_POLA_LEFT) {
      position = adjust_pos(DEVICE_LEFT);
   }
   else if ( (tilt_status & TILT_POLA) == TILT_POLA_RIGHT) {
      position = adjust_pos(DEVICE_RIGHT);
   }
   else if ( (tilt_status & TILT_BAFR) == TILT_BAFR_FRONT) {
      position = DEVICE_FACE_DOWN;
   }
   else if ( (tilt_status & TILT_BAFR) == TILT_BAFR_BACK) {
      position = DEVICE_FACE_UP;
   }

   // Correct position if MMA7660F is loaded on the back side of the board
   if (flip != 0) {
      switch (position) {
      case DEVICE_LEFT:
         position = DEVICE_RIGHT;
      break;
      case DEVICE_RIGHT:
         position = DEVICE_LEFT;
      break;
      case DEVICE_FACE_UP:
         position = DEVICE_FACE_DOWN;
      break;
      case DEVICE_FACE_DOWN:
         position = DEVICE_FACE_UP;
      break;
      }
   }

   if (position != DEVICE_UNKNOWN) {
      device_orientation = position;
      report_orientation(device_orientation);
   }
}


/*
 * This IRQ routine is executed when the INT line is asserted,
 * indicating that data is ready for sampling.
 */

static irqreturn_t mma7660F_irq (int irq, void *data, struct pt_regs *r)
{
   LLOG_DEBUG2("Received INT IRQ from mma7660F.\n");

   // Schedule work function to read device status with no delay
   schedule_delayed_work(&mma7660F_tq_d, 0);

   return IRQ_HANDLED;
}


/*!
 * This function puts the MMA7660F device in low-power mode/state,
 * and it disables interrupts from the input line.
 */
void mma7660F_sleep(void)
{
   LLOG_DEBUG1("Disabling IRQ and putting accelerometer in standby mode...\n");

   // Stop any pending delayed work items
   cancel_delayed_work(&mma7660F_tq_d);

   // Disable IRQ line
   disable_irq(INT_IRQ);

   // Put Freescale MMA7660F Accelerometer in standby mode
   write_mma7660F_reg(ACCEL_MODE_REG, 0);
}


/*!
 * This function brings the MMA7660F device back from low-power state,
 * and re-enables the line interrupt.
 */
void mma7660F_wake(void)
{
   LLOG_DEBUG1("Enabling IRQ and waking mma7660F\n");

   // Enable IRQ line
   enable_irq(INT_IRQ);

   init_mma7660F();

   // Schedule work function to read initial device status after a 2-sec delay
   schedule_delayed_work(&mma7660F_tq_d, (2 * HZ));
}


/*!
 * This function puts the MMA7660F device in low-power mode/state,
 * and it disables interrupts from the input line.
 * @param   pdev  the device structure used to give information on MMA7660F
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function always returns 0.
 */
static int mma7660F_suspend(struct platform_device *pdev, pm_message_t state)
{
   mma7660F_sleep();
   LLOG_INFO("suspend", "", "status=locked\n");
   return 0;
}


/*!
 * This function brings the MMA7660F device back from low-power state,
 * and re-enables the line interrupt.
 *
 * @param   pdev  the device structure used to give information on MMA7660F
 *                to resume
 *
 * @return  The function always returns 0.
 */
static int mma7660F_resume(struct platform_device *pdev)
{
   mma7660F_wake();
   LLOG_INFO("resume", "", "status=unlocked\n");
   return 0;
}


/*
 * This function returns the current orientation reported by the MMA7660F
 */
int mma7660F_proc_read( char *page, char **start, off_t off,
                      int count, int *eof, void *data )
{
   int len;

   if (off > 0) {
     *eof = 1;
     return 0;
   }
   switch (device_orientation) {
   case DEVICE_UNKNOWN:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_UNKNOWN);
      break;
   case DEVICE_UP:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_UP);
      break;
   case DEVICE_DOWN:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_DOWN);
      break;
   case DEVICE_LEFT:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_LEFT);
      break;
   case DEVICE_RIGHT:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_RIGHT);
      break;
   case DEVICE_FACE_UP:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_FACE_UP);
      break;
   case DEVICE_FACE_DOWN:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_FACE_DOWN);
      break;
   case DEVICE_FREE_FALL:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_FREE_FALL);
      break;
   default:
      len = sprintf(page, "%s\n", PROC_ORIENTATION_STRING_INVALID);
      break;
   };

   return len;
}


ssize_t mma7660F_proc_write( struct file *filp, const char __user *buff,
                            unsigned long len, void *data )

{
   char command[PROC_CMD_LEN];
   int  err;
   signed char acc_data[3];
   int acc_data_int;
   unsigned int reg_addr_int;
   unsigned char reg_addr;
   unsigned char shake_or_tap;
   unsigned char axis;
   unsigned int new_hold_time;
   unsigned int calculated_debounce;

   if (len > PROC_CMD_LEN) {
      LLOG_ERROR("proc_len", "", "mma7455L command is too long!\n");
      return -ENOSPC;
   }

   if (copy_from_user(command, buff, len)) {
      return -EFAULT;
   }

   if ( !strncmp(command, "rdreg", 5) ) {
      // Requested to read one of the registers of the MMA7660F
      sscanf(command, "rdreg %x", &reg_addr_int);
      reg_addr = (unsigned char)reg_addr_int;
      err = read_mma7660F_regs(reg_addr, acc_data, 1);
      if (err < 0) {
         LLOG_ERROR("rdreg_fail", "", "ERROR attempting to read MMA7660F register 0x%02X\n", reg_addr);
         return -1;
      }
      else {
         LLOG_INFO("rdreg", "", "Read 0x%02X from register 0x%02X\n", acc_data[0], reg_addr);
      }
   }
   else if ( !strncmp(command, "wrreg", 5) ) {
      // Requested to write to one of the registers of the MMA7660F
      sscanf(command, "wrreg %x %x", &reg_addr_int, &acc_data_int);
      reg_addr = reg_addr_int;
      acc_data[0] = acc_data_int;
      err = write_mma7660F_reg(reg_addr, acc_data[0]);
      if (err < 0) {
         LLOG_ERROR("wrreg_fail", "", "ERROR attempting to write 0x%02X to MMA7660F register 0x%02X\n", acc_data[0], reg_addr);
         return -1;
      }
      else {
         LLOG_INFO("wrreg", "", "Wrote 0x%02X to MMA7660F register 0x%02X\n", acc_data[0], reg_addr);
      }
   }
   else if ( !strncmp(command, "amiflat", 7) ) {
      err = read_mma7660F_regs(ACCEL_XOUT_REG, acc_data, 2);
      if (err < 0) {
         LLOG_ERROR("amiflat_fail", "", "ERROR attempting to read MMA7660F registers 0x00-0x01\n");
         return -1;
      }
      else {
#define FLAT_TOL 2
         acc_data[0] &= XOUT_DATA_MASK;
         acc_data[1] &= YOUT_DATA_MASK;
         LLOG_DEBUG2("x = 0x%x; y = 0x%x\n", acc_data[0], acc_data[1]);
         if ( ((acc_data[0] <= (0 + FLAT_TOL)) || 
               (acc_data[0] >= (0x3F - FLAT_TOL))) &&
              ((acc_data[1] <= (0 + FLAT_TOL)) ||
               (acc_data[1] >= (0x3F - FLAT_TOL))) ) {
            LLOG_INFO("flat", "", "flat=yes\n");
         }
         else {
            LLOG_INFO("notflat", "", "flat=no\n");
         }
      }
   }
   else if ( !strncmp(command, "lock", 4) ) {
      mma7660F_sleep();
      LLOG_INFO("lock", "", "status=locked\n");
   }
   else if ( !strncmp(command, "unlock", 6) ) {
      mma7660F_wake();
      LLOG_INFO("unlock", "", "status=unlocked\n");
   }
   else if ( !strncmp(command, "hold", 4) ) {
      sscanf(command, "hold %d", &new_hold_time);
      // Calculate tilt debounce from existing sample rate and new hold time
      switch(accel_sampling_rate) {
      case SR_AMSR_AM1:
         calculated_debounce = new_hold_time / 1000;
         break;
      case SR_AMSR_AM2:
         calculated_debounce = new_hold_time * 2 / 1000;
         break;
      case SR_AMSR_AM4:
         calculated_debounce = new_hold_time * 4 / 1000;
         break;
      case SR_AMSR_AM8:
         calculated_debounce = new_hold_time * 8 / 1000;
         break;
      case SR_AMSR_AM16:
         calculated_debounce = new_hold_time * 16 / 1000;
         break;
      case SR_AMSR_AM32:
         calculated_debounce = new_hold_time * 32 / 1000;
         break;
      case SR_AMSR_AM64:
         calculated_debounce = new_hold_time * 64 / 1000;
         break;
      case SR_AMSR_AMPD:
         calculated_debounce = new_hold_time * 120 / 1000;
         break;
      default:
         accel_sampling_rate = DEFAULT_ACCEL_SAMPLING_RATE;
         calculated_debounce = 0;
         LLOG_WARN("bad_sr", "", "resetting incorrect sampling rate to default value; try command again\n");
         return(len);
         break;
      }
      if (calculated_debounce > 8) {
         calculated_debounce = 8;
         LLOG_INFO("large_hold", "", "hold time was too large; rounding down...\n");
      }
      switch (calculated_debounce) {
      case 0:
      case 1:
         accel_tilt_debounce = SR_FILT_DISABLED;
         break;
      case 2:
         accel_tilt_debounce = SR_FILT_2;
         break;
      case 3:
         accel_tilt_debounce = SR_FILT_3;
         break;
      case 4:
         accel_tilt_debounce = SR_FILT_4;
         break;
      case 5:
         accel_tilt_debounce = SR_FILT_5;
         break;
      case 6:
         accel_tilt_debounce = SR_FILT_6;
         break;
      case 7:
         accel_tilt_debounce = SR_FILT_7;
         break;
      case 8:
         accel_tilt_debounce = SR_FILT_8;
         break;
      default:
         accel_tilt_debounce = SR_FILT_DISABLED;
         break;
      }
      init_mma7660F();  // Update mma7660F registers with new values
      LLOG_INFO("hold", "", "hold time=%d msec\n", new_hold_time);
   }
   else if ( !strncmp(command, "enable", 6) ) {
      // Requested to enable shake or tap
      sscanf(command, "enable %c %c", &shake_or_tap, &axis);
      if ( (shake_or_tap == 's') || (shake_or_tap == 'S') ) {
         switch(axis) {
         case 'x':
            shake_x_enabled = 1;
            LLOG_INFO("shake_x_en", "", "Enabling shake detection on X axis\n");
            break;
         case 'y':
            shake_y_enabled = 1;
            LLOG_INFO("shake_y_en", "", "Enabling shake detection on Y axis\n");
            break;
         case 'z':
            shake_z_enabled = 1;
            LLOG_INFO("shake_z_en", "", "Enabling shake detection on Z axis\n");
            break;
         default:
            LLOG_ERROR("bad_en_shake_axis", "", "ERROR - shake axis must be x, y, or z\n");
            return(len);
            break;
         }
         init_mma7660F();
      }
      else if ( (shake_or_tap == 't') || (shake_or_tap == 'T') ) {
         switch(axis) {
         case 'x':
            tap_x_enabled = 1;
            LLOG_INFO("tap_x_en", "", "Enabling tap detection on X axis\n");
            break;
         case 'y':
            tap_y_enabled = 1;
            LLOG_INFO("tap_y_en", "", "Enabling tap detection on Y axis\n");
            break;
         case 'z':
            tap_z_enabled = 1;
            LLOG_INFO("tap_z_en", "", "Enabling tap detection on Z axis\n");
            break;
         default:
            LLOG_ERROR("bad_en_tap_axis", "", "ERROR - tap axis must be x, y, or z\n");
            return(len);
            break;
         }
         init_mma7660F();
      }
      else {
         LLOG_ERROR("bad_enable", "", "ERROR - must specify either s for shake or t for tap\n");
      }
   }
   else if ( !strncmp(command, "disable", 6) ) {
      // Requested to disable shake or tap
      sscanf(command, "disable %c %c", &shake_or_tap, &axis);
      if ( (shake_or_tap == 's') || (shake_or_tap == 'S') ) {
         switch(axis) {
         case 'x':
            shake_x_enabled = 0;
            LLOG_INFO("shake_x_dis", "", "Disabling shake detection on X axis\n");
            break;
         case 'y':
            shake_y_enabled = 0;
            LLOG_INFO("shake_y_dis", "", "Disabling shake detection on Y axis\n");
            break;
         case 'z':
            shake_z_enabled = 0;
            LLOG_INFO("shake_z_dis", "", "Disabling shake detection on Z axis\n");
            break;
         default:
            LLOG_ERROR("bad_dis_shake_axis", "", "ERROR - Usage - disable s|t x|y|z \n");
            return(len);
            break;
         }
         init_mma7660F();
      }
      else if ( (shake_or_tap == 't') || (shake_or_tap == 'T') ) {
         switch(axis) {
         case 'x':
            tap_x_enabled = 0;
            LLOG_INFO("tap_x_dis", "", "Disabling tap detection on X axis\n");
            break;
         case 'y':
            tap_y_enabled = 0;
            LLOG_INFO("tap_y_dis", "", "Disabling tap detection on Y axis\n");
            break;
         case 'z':
            tap_z_enabled = 0;
            LLOG_INFO("tap_z_dis", "", "Disabling tap detection on Z axis\n");
            break;
         default:
            LLOG_ERROR("bad_dis_tap_axis", "", "ERROR - tap axis must be x, y, or z\n");
            return(len);
            break;
         }
         init_mma7660F();
      }
      else {
         LLOG_ERROR("bad_disable", "", "ERROR - must specify either s for shake or t for tap\n");
      }
   }
   else if ( !strncmp(command, "debug", 5) ) {
      // Requested to set debug level
      sscanf(command, "debug %d", &debug);
      LLOG_INFO("debug", "", "Setting debug level to %d\n", debug);
      set_debug_log_mask(debug);
   }
   else {
      LLOG_ERROR("proc_cmd", "", "Unrecognized command\n");
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
static int __devinit mma7660F_probe(struct platform_device *pdev)
{
   int rqstatus = 0;
   int error;

   LLOG_INFO("probe_st", "", "Starting...\n");
   /* Create proc entry */
   proc_entry = create_proc_entry(ACCEL_PROC_FILE, 0644, NULL);
   if (proc_entry == NULL) {
      LLOG_ERROR("proc_entry_fail", "", "Couldn't create proc entry\n");
      return -ENOMEM;
   } else {
      proc_entry->read_proc = mma7660F_proc_read;
      proc_entry->write_proc = mma7660F_proc_write;
      proc_entry->owner = THIS_MODULE;
   }

   // Initialize the array that contains the information for the
   // MMA7660F lines of the various hardware platforms
   line_info[MARIO] = MARIO_INT_GPIO;
   line_info[NELL]  = NELL_INT_GPIO;

   // Set dinamically the line informarion depending on the version passed
   // as a parameter
   if ((hw_rev >= 0) && (hw_rev < MAX_VERSIONS)) {
      int_gpio = line_info[hw_rev];
   }
   else {
      LLOG_ERROR("bad_hw_rev", "", "hw_rev=%d is invalid; must be 0-%d\n", hw_rev, (MAX_VERSIONS-1));
      int_gpio = (iomux_pin_name_t) NULL;
      return -1;
   }

   // Set the IRQ and the MMA7660F GPIO line as input with falling-edge
   // detection.  Register the IRQ service routine.
   if (int_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(INT_IRQ, IRQF_TRIGGER_FALLING);
      rqstatus = request_irq(INT_IRQ, (irq_handler_t) mma7660F_irq, 0, "mma7660F", NULL);
      if (rqstatus != 0) {
         LLOG_ERROR("irq_req_fail", "", "request_irq failed; INT IRQ line = 0x%X; request status = %d\n", INT_IRQ, rqstatus);
      }
      if (mxc_request_iomux(int_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         LLOG_ERROR("req_iomux_fail", "", "mxc_request_iomux failed; could not obtain GPIO pin for MMA7660F INT line\n");
      }
      else {
          mario_enable_pullup_pad(int_gpio);
          mxc_set_gpio_direction(int_gpio, 1);
      }
   }
   else {
      LLOG_WARN("int_null", "", "MMA7660F INT GPIO defined as NULL; will not be able to take interrupts from chip\n");
   }

   LLOG_INFO("lines_done", "", "GPIO and IRQ have been set up\n");

   // Initialize the work item
   INIT_DELAYED_WORK(&mma7660F_tq_d, detect_mma7660F_pos);

   // Register the device file
   error = misc_register(&mma7660F_dev);
   if (error) {
      LLOG_ERROR("reg_dev", "", "failed to register device mma7660F_dev\n");
      return error;
   }

   // Initialize the MMA7660F
   init_mma7660F();

   // Schedule work function to read initial device status after a 2-sec delay
   schedule_delayed_work(&mma7660F_tq_d, (2 * HZ));

   LLOG_INFO("dev_initd", "", "MMA7660F device has been initialized\n");

   return 0;
}

/*!
 * MMA7660F I2C detect_client function
 *
 * @param adapter            struct i2c_adapter *
 * @param address            int
 * @param kind               int
 *
 * @return  Error code indicating success or failure
 */
static int mma7660F_detect_client(struct i2c_adapter *adapter, int address,
                                 int kind)
{
	int err;
	signed char acc_buff[3];

        mma7660F_i2c_client.adapter = adapter;
#ifdef NOT_NEEDED
        err = i2c_attach_client(&mma7660F_i2c_client);
        if (err) {
                mma7660F_i2c_client.adapter = NULL;
                LLOG_ERROR("i2c_attach_fail", "", "i2c_attach_client failed; err = 0x%0X\n", err);
                return err;
        }
#endif

        // Can we talk to the device?
        err = read_mma7660F_regs(ACCEL_MODE_REG, acc_buff, 1);
        if (err < 0) {
                mma7660F_reads_OK = -1;
                return -1;
        }

        LLOG_INFO("dev_detected", "", "Freescale MMA7660F accelerometer detected\n");

        return 0;
}

/* Address to scan; addr_data points to this variable */
static unsigned short normal_i2c[] = { ACCEL_I2C_ADDRESS, I2C_CLIENT_END };

/* Magic definition of all other variables and things */
//I2C_CLIENT_INSMOD;
I2C_CLIENT_INSMOD_1(mma7660F);

/*!
 * MMA7660F I2C attach function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int mma7660F_attach(struct i2c_adapter *adap)
{
        int err;

        err = i2c_probe(adap, &addr_data, mma7660F_detect_client);

        err = mma7660F_detect_client(adap, mma7660F_i2c_client.addr, 0);

        return err;
}

/*!
 * MMA7660F I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int mma7660F_detach(struct i2c_client *client)
{
        int err;

        if (!mma7660F_i2c_client.adapter)
                return -1;

        err = i2c_detach_client(&mma7660F_i2c_client);
        mma7660F_i2c_client.adapter = NULL;

        return err;
}



/*!
 * Dissociates the driver from the MMA7660F Accelerometer device.
 *
 * @param   pdev  the device structure used to give information on which SDHC
 *                to remove
 *
 * @return  The function always returns 0.
 */
static int __devexit mma7660F_remove(struct platform_device *pdev)
{
   // Stop any pending delayed work items
   cancel_delayed_work(&mma7660F_tq_d);

   // Put MMA7660F in standby mode
   write_mma7660F_reg(ACCEL_MODE_REG, 0);

   // Release GPIO pins and IRQs
   if (int_gpio != (iomux_pin_name_t)NULL) {
      free_irq(INT_IRQ, NULL);
      mario_disable_pullup_pad(int_gpio);
      mxc_free_iomux(int_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
   }

   remove_proc_entry(ACCEL_PROC_FILE, &proc_root);
   misc_deregister(&mma7660F_dev);

   // Unregister i2c device
   i2c_del_driver(&mma7660F_i2c_driver);

   LLOG_INFO("exit", "", "IRQ released and device disabled.\n");

   return 0;
}


/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mma7660F_driver = {
        .driver = {
                   .name = "mma7660F",
                   .owner  = THIS_MODULE,
                   //.bus = &platform_bus_type,
                   },
        .suspend = mma7660F_suspend,
        .resume  = mma7660F_resume,
        .probe   = mma7660F_probe,
        .remove  = __devexit_p(mma7660F_remove),
};



/*!
 * This function is called for module initialization.
 * It registers the MMA7660F char driver.
 *
 * @return      0 on success and a non-zero value on failure.
 */
static int __init mma7660F_init(void)
{
	int err;

	LLOG_INIT();
	set_debug_log_mask(debug);
        err = platform_driver_register(&mma7660F_driver);
        if (err < 0) {
           platform_driver_unregister(&mma7660F_driver);
	   return err;
	}
	err = i2c_add_driver(&mma7660F_i2c_driver);
        if (err < 0) {
	   i2c_del_driver(&mma7660F_i2c_driver);
	   return err;
	}
	if (mma7660F_reads_OK < 0) {
           LLOG_ERROR("not_found", "", "Freescale MMA7660F Accelerometer device not found\n");
	   i2c_del_driver(&mma7660F_i2c_driver);
           platform_driver_unregister(&mma7660F_driver);
	   return -ENODEV;
	}
        mma7660F_probe(NULL);
        LLOG_INFO("drv", "", "Freescale MMA7660F Accelerometer driver loaded\n");
        return 0;
}

/*!
 * This function is called whenever the module is removed from the kernel. It
 * unregisters the MMA7660F driver from kernel.
 */
static void __exit mma7660F_exit(void)
{
        mma7660F_remove(NULL);
        platform_driver_unregister(&mma7660F_driver);
}



module_init(mma7660F_init);
module_exit(mma7660F_exit);

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("Freescale MMA7660F Accelerometer Device Driver");
MODULE_LICENSE("GPL");
