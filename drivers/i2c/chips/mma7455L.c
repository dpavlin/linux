/*
** Freescale MMA7455L Accelerometer driver routine for Mario.  (C) 2008 Lab126
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
#include <linux/input-polldev.h>
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

#include "mma7455L.h"

typedef u32 * volatile reg_addr_t;

// Device structure
static struct miscdevice mma7455L_dev = { MISC_DYNAMIC_MINOR, "mma7455L",
                                          NULL,
                                        };

// Global variables

static char device_orientation = DEVICE_INIT_ORIENTATION;
static char previous_orientation = DEVICE_INIT_ORIENTATION;
static char orientation_state = ORIENTATION_INIT;
static char polling_state = POLL_INIT;
static int hold_time_remaining;
static int mma7455L_reads_OK = 0;

// Proc entry structure
#define ACCEL_PROC_FILE "accelerometer"
#define PROC_CMD_LEN 50
static struct proc_dir_entry *proc_entry;

// Workqueue
static struct delayed_work mma7455L_tq_d;
static void mma7455L_poll(struct work_struct *);



// The following array contains information for the MMA7455L Accelerometer
// lines for each different hardware revision.
// A NULL value in any element indicates that the line is not a GPIO;
// it may be unused, or it may be not connected.  In this case, we will not
// be able to take interrupts from such a line.
static iomux_pin_name_t line_info[MAX_VERSIONS][MAX_LINES];

static iomux_pin_name_t int1_drdy_gpio;
static iomux_pin_name_t int2_gpio;

// i2c-related variables
static int mma7455L_attach(struct i2c_adapter *adapter);
static int mma7455L_detach(struct i2c_client *client);


/*!
 * This structure is used to register the device with the i2c driver
 */
static struct i2c_driver mma7455L_i2c_driver = {
        .driver = {
                   .owner = THIS_MODULE,
                   .name = "Freescale MMA7455L Accelerometer Client",
                   },
        .attach_adapter = mma7455L_attach,
        .detach_client = mma7455L_detach,
};

/*!
 * This structure is used to register the client with the i2c driver
 */
static struct i2c_client mma7455L_i2c_client = {
        .name = "Accel I2C dev",
        .addr = ACCEL_I2C_ADDRESS,
        .driver = &mma7455L_i2c_driver,
};


// Module parameters

#define HW_REV_DEFAULT 1
static int hw_rev = HW_REV_DEFAULT;
module_param(hw_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hw_rev, "System hardware revision; 0=Mario, 1=Nell (default is 1)");

#define SAMPLING_TIME_DEFAULT 200
static int sampling_time = SAMPLING_TIME_DEFAULT;
module_param(sampling_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(sampling_time, "Time between sampling Freescale MMA7455L Accelerometer, in msec (default is 200)");

#define FIRST_SAMPLING_TIME_DEFAULT 5000
static int first_sampling_time = FIRST_SAMPLING_TIME_DEFAULT;

#define I2C_SAMPLING_TIME_DEFAULT 20
static int i2c_sampling_time = I2C_SAMPLING_TIME_DEFAULT;

#define HOLD_TIME_DEFAULT 1000
static int hold_time = HOLD_TIME_DEFAULT;
module_param(hold_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(hold_time, "Time to wait before reporting a new device orientation, in msec (default is 1000)");

#define SENSITIVITY_DEFAULT 31
static int sensitivity = SENSITIVITY_DEFAULT;
module_param(sensitivity, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(sensitivity, "Sensitivity for locking a detected position (default is 31)");

#define ROTATION_DEFAULT 0
static int rotation = ROTATION_DEFAULT;
module_param(rotation, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rotation, "Rotation, in degrees, to redefine which way is 'up' (default is 0 and all values will be rounded to the nearest multiple of 90, and mod 360)");

#define FLIP_DEFAULT 0
static int flip = FLIP_DEFAULT;
module_param(flip, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(flip, "Change the definition of face_up and face_down; 0 = face_up is with IC mounted on top of motherboard; any other value = with IC mounted on bottom of motherboard (default is 0)");

#define X_OFFSET_DEFAULT 0
static int x_offset = X_OFFSET_DEFAULT;
module_param(x_offset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(x_offset, "Offset value for calibration of MMA7455L x-axis measurements (default is 0)");

#define Y_OFFSET_DEFAULT 0
static int y_offset = Y_OFFSET_DEFAULT;
module_param(y_offset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(y_offset, "Offset value for calibration of MMA7455L y-axis measurements (default is 0)");

#define Z_OFFSET_DEFAULT 0
static int z_offset = Z_OFFSET_DEFAULT;
module_param(z_offset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(z_offset, "Offset value for calibration of MMA7455L z-axis measurements (default is 0)");

static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");


/*!
 * Read or Write data to/from the MMA7455L via the i2c interface
 * @param   reg_addr  address of the register to be written
 * @param   wr_data   data to be written
 *
 * @return  The function returns 0 if successful,
 *          or a negative number on failure
 */
static int mma7455L_i2c_client_xfer(unsigned int addr, char *reg,
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

        ret = i2c_transfer(mma7455L_i2c_client.adapter, msg, 2);
        if (ret >= 0)
                return 0;

        return ret;
}



/*!
 * Write a byte to the requested MMA7455L register
 * @param   reg_addr  address of the register to be written
 * @param   wr_data   data to be written
 *
 * @return  The function returns 0 or a positive integer if successful,
 *          or a negative number on failure
 */

static int write_mma7455L_reg(unsigned char reg_addr, char wr_data)
{
   unsigned char mma7455L_reg_addr;
   char mma7455L_wr_data[ACCEL_REG_DATA_LEN];
   int ret_code;

   mma7455L_reg_addr = reg_addr;
   mma7455L_wr_data[0] = wr_data;
   ret_code = mma7455L_i2c_client_xfer(ACCEL_I2C_ADDRESS, &mma7455L_reg_addr,
              ACCEL_REG_ADDR_LEN, mma7455L_wr_data, ACCEL_REG_DATA_LEN, 
              MXC_I2C_FLAG_WRITE);
   if (debug > 3) {
      printk("<1>[write_mma7455L_reg]Wrote 0x%X to register 0x%X; ret_code = 0x%X\n", mma7455L_wr_data[0], mma7455L_reg_addr, ret_code);
   }
   if (ret_code < 0) {
      printk("<1>[write_mma7455L_reg] ERROR: Attempted writing 0x%X to MMA7455L register 0x%X; failed with ret_code = 0x%X\n", mma7455L_wr_data[0], mma7455L_reg_addr, ret_code);
   }

   return ret_code;
}


/*!
 * Read a byte from the requested MMA7455L register
 * @param   reg_addr  address of the register to be read
 * @param   *rd_buff pointer to register or buffer where data read is returned
 * @param   count    number of bytes to read (if sequential registers read)
 *
 * @return  The function returns 0 or a positive integer if successful,
 *          or a negative number on failure
 *          NOTE: data read is returned in rd_buff
 */

static int read_mma7455L_regs(unsigned char reg_addr, char *rd_buff, int count)
{
   unsigned char mma7455L_reg_addr;
   int ret_code;
   int i;

   mma7455L_reg_addr = reg_addr;
   ret_code = mma7455L_i2c_client_xfer(ACCEL_I2C_ADDRESS, &mma7455L_reg_addr,
              ACCEL_REG_ADDR_LEN, rd_buff, count, MXC_I2C_FLAG_READ);
   if ((ret_code < 0) && (debug > 0)) {
      printk("<1>[read_mma7455L_reg] ERROR: Attempted reading from MMA7455L register 0x%X; failed with ret_code = 0x%X\n", mma7455L_reg_addr, ret_code);
   }
   else {
      // Read successful
      if (debug > 3) {
         for (i=0; i<count; i++) {
            printk("<1>[read_mma7455L_reg] Read 0x%X from register 0x%X; ret_code = 0x%X\n", rd_buff[i], mma7455L_reg_addr+i, ret_code);
         }
      }
   }

   return ret_code;

}


/*
 * Put the Freescale MMA7455L Accelerometer in measurement mode
 */

void wake_mma7455L(void)
{
   // Put MMA7455L in default Control 1 mode, ie
   // All axes enabled, INT1 set for level detection, absolute thresold
   // value, and digital filter band width 62.5Hz
   //write_mma7455L_reg(ACCEL_CTL1_REG, 0);
   // Clear the interrupt flags
   write_mma7455L_reg(ACCEL_INTRST_REG, INTRST_CLR_INT1 | INTRST_CLR_INT2);
   write_mma7455L_reg(ACCEL_INTRST_REG, 0);
   // Put MMA7455L in measurement mode, 2G measurement range
   write_mma7455L_reg(ACCEL_MCTL_REG, MEASUREMENT_MODE | MEASUREMENT_RANGE_2G);
}


/*
 * Put Freescale MMA7455L Accelerometer in sleep mode
 */

void sleep_mma7455L(void)
{
   // Put MMA7455L in sleep mode
   write_mma7455L_reg(ACCEL_MCTL_REG, 0);
}


/*
 * Correct the device's position, based on the orientation of the device
 * on the motherboard.
 */

char adjust_pos(char position, int rot_index)
{
   // Make any necessary adjustments based on rotation
   switch (rot_index) {
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
 * Read the x,y, and z registers of the MMA7455L and determine the
 * device's orientation.
 * Returns the orientation as a char, based on the enum found on 
 * mma7455L.h
 */

static char detect_mma7455L_pos(void)
{
   signed char acc_buff[3];
   int x_reading, y_reading, z_reading;
   char position;
   int rotation_index;

   // Check INT1/DRDY line to see if MMA7455L data is ready
   
   //if ( mxc_get_gpio_datain(int1_drdy_gpio) == 0) {
      //if (debug > 1) {
         //printk("<1>[detect_mma7455L_pos] NOTE: Device not ready - exiting...\n");
      //}
      //return DEVICE_UNKNOWN;
   //}

   // Get x, y, and z readings from mma7455L
   read_mma7455L_regs(ACCEL_XOUT8_REG, acc_buff, 3);

   // Assign axis readings, and compensate with calibration offset
   x_reading = acc_buff[0] + x_offset;
   y_reading = acc_buff[1] + y_offset;
   z_reading = acc_buff[2] + z_offset;

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

   if (debug > 1) {
      printk("<1>[detect_mma7455L_pos] x_reading = 0x%X, y_reading = 0x%X, z_reading = 0x%X\n", x_reading, y_reading, z_reading);
   }

   if ( (abs(x_reading + 0x40) < sensitivity) && (abs(y_reading) < sensitivity)) {
      position = adjust_pos(DEVICE_LEFT, rotation_index);
   }
   else if ( (abs(x_reading - 0x40) < sensitivity) && (abs(y_reading) < sensitivity)) {
      position = adjust_pos(DEVICE_RIGHT, rotation_index);
   }
   else if ( (abs(x_reading) < sensitivity) && (abs(y_reading + 0x40) < sensitivity)) {
      position = adjust_pos(DEVICE_UP, rotation_index);
   }
   else if ( (abs(x_reading) < sensitivity) && (abs(y_reading - 0x40) < sensitivity)) {
      position = adjust_pos(DEVICE_DOWN, rotation_index);
   }
   else if ( (abs(x_reading) < sensitivity) && (abs(y_reading) < sensitivity) &&
             (abs(z_reading - 0x40) < sensitivity)) {
      position = DEVICE_FACE_UP;
   }
   else if ( (abs(x_reading) < sensitivity) && (abs(y_reading) < sensitivity) &&
             (abs(z_reading + 0x40) < sensitivity)) {
      position = DEVICE_FACE_DOWN;
   }
   else if ( (abs(x_reading) < sensitivity) && (abs(y_reading) < sensitivity) &&
             (abs(z_reading) < sensitivity)) {
      //position = DEVICE_FREE_FALL;
      // Not reporting FREE_FALL for now...
      position = DEVICE_UNKNOWN;
   }
   else {
      position = DEVICE_UNKNOWN;
   }

   // Correct position if device is "flipped"
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

   if (debug > 1) {
      switch (position) {
      case DEVICE_UP:
         printk("<1>[detect_mma7455L_pos] UP\n");
      break;
      case DEVICE_DOWN:
         printk("<1>[detect_mma7455L_pos] DOWN\n");
      break;
      case DEVICE_LEFT:
         printk("<1>[detect_mma7455L_pos] LEFT\n");
      break;
      case DEVICE_RIGHT:
         printk("<1>[detect_mma7455L_pos] RIGHT\n");
      break;
      case DEVICE_FACE_UP:
         printk("<1>[detect_mma7455L_pos] FACE UP\n");
      break;
      case DEVICE_FACE_DOWN:
         printk("<1>[detect_mma7455L_pos] FACE DOWN\n");
      break;
      case DEVICE_FREE_FALL:
         printk("<1>[detect_mma7455L_pos] FREE FALL!\n");
      break;
      }
   }

   return position;
}

/*
 * Send a uevent for any processes listening in the userspace
 */
static void report_event(char *value)
{
        char *envp[] = {value, "DRIVER=accelerometer", NULL};
        // Send uevent to user space
        if( kobject_uevent_env(&mma7455L_dev.this_device->kobj, KOBJ_CHANGE, envp) )
        {
           printk( "<1>ERROR: mma7455L accelerometer failed to send uevent\n" );
        }
}


/*
 * Send orientation through the input channel
 */

static void report_orientation(char orientation, char previous_orientation)
{
   switch (orientation) {
   case DEVICE_UNKNOWN:
      printk("<1>[report_orientation] Device orientation UNKNOWN\n");
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
      printk("<1>[report_orientation] INVALID VALUE PASSED\n");
      break;
   };
   if (debug > 0) {
      printk("<1>[report_orientation] Device orientation is now ");
      switch (orientation) {
      case DEVICE_UNKNOWN:
         printk("UNKNOWN\n");
         break;
      case DEVICE_UP:
         printk("UP\n");
         break;
      case DEVICE_DOWN:
         printk("DOWN\n");
         break;
      case DEVICE_LEFT:
         printk("LEFT\n");
         break;
      case DEVICE_RIGHT:
         printk("RIGHT\n");
         break;
      case DEVICE_FACE_UP:
         printk("FACE_UP\n");
         break;
      case DEVICE_FACE_DOWN:
         printk("FACE_DOWN\n");
         break;
      case DEVICE_FREE_FALL:
         printk("FREE_FALL\n");
         break;
      default:
         printk("INVALID VALUE PASSED\n");
         break;
      };
   }
}


/*
 * This is the polling or "work" function called periodically to 
 * wake up the MMA7455L, reading its registers, and reporting the
 * device's orientation to the system.
 */

static void mma7455L_poll(struct work_struct *work)
{
   char mma7455L_orientation;

   switch (polling_state) {
   case POLL_SLEEP:
      if (debug > 2) {
         printk("<1>[mma7455L_poll] POLL_SLEEP -> POLL_WAITING_FOR_IRQ; waking up MMA7455L\n");
      }

      polling_state = POLL_WAITING_FOR_IRQ;

      // Put Freescale MMA7455L in measurement mode
      // We will get an interrupt as soon as data is ready; this will be
      // handled by mma7455L_irq
      wake_mma7455L();

      // Enable IRQ line
      //enable_irq(INT1_DRDY_IRQ);

      break;
   case POLL_WAITING_FOR_IRQ:
      if (debug > 2) {
         printk("<1>[mma7455L_poll] POLL_WAITING_FOR_IRQ\n");
      }
      break;
   case POLL_TIME_TO_READ_I2C:
      if (debug > 2) {
         printk("<1>[mma7455L_poll] POLL_TIME_TO_READ_I2C - doing it!\n");
      }
      // Get the current orientation of the MMA7455L
      mma7455L_orientation = detect_mma7455L_pos();

      if (debug > 2) {
         printk("<1>[mma7455L_poll] mma7455L_orientation = %d\n", mma7455L_orientation);
      }

      // Device orientation state machine
      switch (orientation_state) {
      case ORIENTATION_INIT:
         device_orientation = mma7455L_orientation;
         // Force DEVICE_UP if we do not get a definite orientation here
         if (device_orientation == DEVICE_UNKNOWN) {
            device_orientation = DEVICE_UP;
         }
         report_orientation(device_orientation, previous_orientation);
         previous_orientation = device_orientation;
         orientation_state = ORIENTATION_IDLE;
         if (debug > 1) {
            printk("<1>[mma7455L_poll] orientation_state INIT -> IDLE\n");
         }
         break;
      case ORIENTATION_IDLE:
         // Update orientation only if transitioning to a new, known position
         if ( (mma7455L_orientation != DEVICE_UNKNOWN) &&
              ((mma7455L_orientation != device_orientation) ||
               (device_orientation == DEVICE_UNKNOWN)) ) {
            device_orientation = mma7455L_orientation;
            if (mma7455L_orientation == DEVICE_FREE_FALL) {
               // If in free fall, report state immediately!
               hold_time_remaining = 0;
            }
            else {
               hold_time_remaining = hold_time - sampling_time;
            }
            if (hold_time_remaining > 0) {
               // Do not report the new orientation yet
               orientation_state = ORIENTATION_COUNTING;
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state IDLE -> COUNTING; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
            else {
               // Hold time is <= sampling time; report change now
               report_orientation(device_orientation, previous_orientation);
               previous_orientation = device_orientation;
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state stays IDLE; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
         }
         break;
      case ORIENTATION_COUNTING:
         // If orientation changed to unknown or back to the previous orientation,
         // do not report orientation previously detected.
         if ( (mma7455L_orientation == DEVICE_UNKNOWN) ||
              (mma7455L_orientation == previous_orientation) ) {
            device_orientation = mma7455L_orientation;
            orientation_state = ORIENTATION_IDLE;
            if (debug > 1) {
               printk("<1>[mma7455L_poll] orientation_state COUNTING -> IDLE\n");
            }
         }
         else if (mma7455L_orientation == device_orientation) {
            // Holding device in new orientation; see if hold count is done
            hold_time_remaining -= sampling_time;
            if (hold_time_remaining > 0) {
               // Do not report the new orientation yet
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state COUNTING; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
            else {
               // Hold time is expired; report new orientation
               device_orientation = mma7455L_orientation;
               report_orientation(device_orientation, previous_orientation);
               previous_orientation = device_orientation;
               orientation_state = ORIENTATION_IDLE;
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state COUNTING -> IDLE; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
         }
         else {
            // New orientation detected
            device_orientation = mma7455L_orientation;
            if (mma7455L_orientation == DEVICE_FREE_FALL) {
               // If in free fall, report state immediately!
               hold_time_remaining = 0;
            }
            else {
               hold_time_remaining = hold_time - sampling_time;
            }
            if (hold_time_remaining > 0) {
               // Do not report the new orientation yet
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state stays COUNTING; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
            else {
               // Hold time is <= sampling time; report change now
               report_orientation(device_orientation, previous_orientation);
               previous_orientation = device_orientation;
               orientation_state = ORIENTATION_IDLE;
               if (debug > 1) {
                  printk("<1>[mma7455L_poll] orientation_state COUNTING -> IDLE; hold_time_remaining = %d\n", hold_time_remaining);
               }
            }
         }
         break;
      default:
         // Should not get here, but if we do, reset state machine
         printk("<1>[mma7455L_poll] Invalid orientation state; resetting to ORIENTATION_INIT\n");
         orientation_state = ORIENTATION_INIT;
         break;
      }

      // Put Freescale MMA7455L Accelerometer in standby mode, to save
      // power between polling timeouts
      sleep_mma7455L();

      if (debug > 2) {
         printk("<1>[mma7455L_poll] POLL_TIME_TO_READ_I2C -> POLL_SLEEP\n");
      }
      // Update the state variable
      polling_state = POLL_SLEEP;

      // Schedule work function to wake up for the next sampling
      schedule_delayed_work(&mma7455L_tq_d, (sampling_time * HZ)/1000);
      //disable_irq(INT1_DRDY_IRQ);
      break;
   default:
      if (debug > 0) {
         printk("<1>[mma7455L_poll] NOTE: Unexpected polling_state; reset to POLL_SLEEP\n");
      }
      polling_state = POLL_SLEEP;

      // Schedule work function to wake up for the next sampling
      schedule_delayed_work(&mma7455L_tq_d, (sampling_time * HZ)/1000);
      //disable_irq(INT1_DRDY_IRQ);
      break;
   }
}


/*
 * This IRQ routine is executed when the INT1/DRDY line is asserted,
 * indicating that data is ready for sampling.
 */

static irqreturn_t mma7455L_irq (int irq, void *data, struct pt_regs *r)
{
   if (debug > 2) {
      printk("<1>[mma7455L_irq] Received INT1/DRDY IRQ from mma7455L.\n");
      printk("               POLL_WAITING_FOR_IRQ -> POLL_TIME_TO_READ_I2C\n");
   }

   // Update the polling state variable for the polling state machine
   polling_state = POLL_TIME_TO_READ_I2C;

   // Schedule work function to read device status
   schedule_delayed_work(&mma7455L_tq_d, (i2c_sampling_time * HZ)/1000);

   // Disable IRQ line
   //disable_irq(INT1_DRDY_IRQ);

   return IRQ_HANDLED;
}


/*!
 * This function puts the MMA7455L device in low-power mode/state,
 * and it disables interrupts from the input lines.
 * @param   pdev  the device structure used to give information on MMA7455L
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function always returns 0.
 */
static int mma7455L_suspend(struct platform_device *pdev, pm_message_t state)
{
   if (debug > 1) {
      printk("<1>[mma7455L_suspend] Stopping work queue and disabling IRQs...\n");
   }

   // Stop any pending delayed work items
   cancel_delayed_work(&mma7455L_tq_d);

   // Disable IRQ line
   //disable_irq(INT1_DRDY_IRQ);

   // Put Freescale MMA7455L Accelerometer in standby mode
   sleep_mma7455L();

   // Update the state variable
   polling_state = POLL_SLEEP;

   return 0;
}


/*!
 * This function brings the MMA7455L device back from low-power state,
 * and re-enables the line interrupts.
 *
 * @param   pdev  the device structure used to give information on MMA7455L
 *                to resume
 *
 * @return  The function always returns 0.
 */
static int mma7455L_resume(struct platform_device *pdev)
{
   if (debug > 1) {
      printk("<1>[mma7455L_resume] Enabling IRQs...\n");
   }

   // Update the state variable
   polling_state = POLL_SLEEP;

   // Schedule work function to read initial device status after a delay
   schedule_delayed_work(&mma7455L_tq_d, (first_sampling_time * HZ)/1000);
   //disable_irq(INT1_DRDY_IRQ);

   return 0;
}


/*
 * This function returns the current state of the fiveway device
 */
int mma7455L_proc_read( char *page, char **start, off_t off,
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

ssize_t mma7455L_proc_write( struct file *filp, const char __user *buff,
                            unsigned long len, void *data )

{
   char command[PROC_CMD_LEN];
   int  err;
   signed char acc_data;
   int acc_data_int;
   unsigned int reg_addr_int;
   unsigned char reg_addr;

   if (len > PROC_CMD_LEN) {
      //LLOG_ERROR("proc_len", "", "Fiveway: command is too long!\n");
      printk("<1>Fiveway: command is too long!\n");
      return -ENOSPC;
   }

   if (copy_from_user(command, buff, len)) {
      return -EFAULT;
   }

   if ( !strncmp(command, "rdreg", 5) ) {
      // Requested to read one of the registers of the MMA7455L
      sscanf(command, "rdreg %x", &reg_addr_int);
      reg_addr = (unsigned char)reg_addr_int;
      err = read_mma7455L_regs(reg_addr, &acc_data, 1);
      if (err < 0) {
         printk("<1>ERROR attempting to read MMA7455L register 0x%02X\n", reg_addr);
         return -1;
      }
      else {
         printk("<1>Read 0x%2X from register 0x%02X\n", acc_data, reg_addr);
      }
   }
   else if ( !strncmp(command, "wrreg", 5) ) {
      // Requested to write to one of the registers of the MMA7455L
      sscanf(command, "wrreg %x %x", &reg_addr_int, &acc_data_int);
      reg_addr = reg_addr_int;
      acc_data = acc_data_int;
      err = write_mma7455L_reg(reg_addr, acc_data);
      if (err < 0) {
         printk("<1>ERROR attempting to write 0x%2X to MMA7455L register 0x%02X\n", acc_data, reg_addr);
         return -1;
      }
      else {
         printk("<1>Wrote 0x%2X to MMA7455L register 0x%02X\n", acc_data, reg_addr);
      }
   }
   else if ( !strncmp(command, "debug", 5) ) {
      // Requested to set debug level
      sscanf(command, "debug %d", &debug);
      //LLOG_INFO("debug", "", "Setting debug level to %d\n", debug);
      //set_debug_log_mask(debug);
   }
   else {
      //LLOG_ERROR("proc_cmd", "", "Unrecognized fiveway command\n");
      printk("<1>Unrecognized fiveway command\n");
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
static int __devinit mma7455L_probe(struct platform_device *pdev)
{
   int rqstatus = 0;
   int error;

   printk("<1>[mma7455L_probe] Starting...\n");
   /* Create proc entry */
   proc_entry = create_proc_entry(ACCEL_PROC_FILE, 0644, NULL);
   if (proc_entry == NULL) {
      printk(KERN_INFO "mma7455L: Couldn't create proc entry\n");
      return -ENOMEM;
   } else {
      proc_entry->read_proc = mma7455L_proc_read;
      proc_entry->write_proc = mma7455L_proc_write;
      proc_entry->owner = THIS_MODULE;
   }

   // Initialize the array that contains the information for the
   // MMA7455L lines of the various hardware platforms
   line_info[MARIO][INT1_DRDY] = MARIO_INT1_DRDY_GPIO;
   line_info[MARIO][INT2]      = MARIO_INT2_GPIO;

   line_info[NELL][INT1_DRDY]  = NELL_INT1_DRDY_GPIO;
   line_info[NELL][INT2]       = NELL_INT2_GPIO;

   // Set dinamically the line informarion depending on the version passed
   // as a parameter
   if ((hw_rev >= 0) && (hw_rev < MAX_VERSIONS)) {
      int1_drdy_gpio = line_info[hw_rev][INT1_DRDY];
      int2_gpio      = line_info[hw_rev][INT2];
   }
   else {
      printk("<1>[mma7455L_init] *** ERROR: hw_rev=%d is invalid; must be 0-%d\n", hw_rev, (MAX_VERSIONS-1));
      int1_drdy_gpio = (iomux_pin_name_t) NULL;
      int2_gpio      = (iomux_pin_name_t) NULL;
      return -1;
   }

   // Set IRQ for MMA7455L GPIO lines; trigger on falling edges
   // Set the MMA7455L GPIO lines as inputs with rising-edge detection,
   // and register the IRQ service routines
   if (int1_drdy_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(INT1_DRDY_IRQ, IRQF_TRIGGER_RISING);
      rqstatus = request_irq(INT1_DRDY_IRQ, (irq_handler_t) mma7455L_irq, 0, "mma7455L", NULL);
      if (rqstatus != 0) {
         printk("<1>[mma7455L_probe] *** ERROR: INT1_DRDY IRQ line = 0x%X; request status = %d\n", INT1_DRDY_IRQ, rqstatus);
      }
      if (mxc_request_iomux(int1_drdy_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[mma7455L_probe] *** ERROR: could not obtain GPIO pin for MMA7455L INT1_DRDY line\n");
      }
      else {
          mxc_set_gpio_direction(int1_drdy_gpio, 1);
      }
   }
   else {
      printk("<1>[mma7455L_probe] *** NOTE: MMA7455L INT1_DRDY GPIO defined as NULL\n");
   }

   if (int2_gpio != (iomux_pin_name_t)NULL) {
      set_irq_type(INT2_IRQ, IRQF_TRIGGER_RISING);
      rqstatus = request_irq(INT2_IRQ, (irq_handler_t) mma7455L_irq, 0, "mma7455L", NULL);
      if (rqstatus != 0) {
         printk("<1>[mma7455L_probe] *** ERROR: INT2 IRQ line = 0x%X; request status = %d\n", INT2_IRQ, rqstatus);
      }
      if (mxc_request_iomux(int2_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
         printk("<1>[mma7455L_probe] *** ERROR: could not obtain GPIO pin for MMA7455L INT2 line\n");
      }
      else {
          mxc_set_gpio_direction(int2_gpio, 1);
      }
   }
   else {
      printk("<1>[mma7455L_probe] *** NOTE: MMA7455L INT2 GPIO defined as NULL\n");
   }

   printk("<1>[mma7455L_probe] GPIOs and IRQs have been set up\n");

   // Initialize the work item
   INIT_DELAYED_WORK(&mma7455L_tq_d, mma7455L_poll);

   // Register the device file
   error = misc_register(&mma7455L_dev);
   if (error) {
      printk(KERN_ERR "failed to register device mma7455L_dev \n");
      return error;
   }

   // Initialize hold time variable
   hold_time_remaining = hold_time;

   // Put the MMA7455L in low-power mode; it will be put in
   // measurement mode when the polling timer is activated
   sleep_mma7455L();

   // Initialize the polling state
   polling_state = POLL_SLEEP;

   // Schedule work function to read initial device status after a delay
   schedule_delayed_work(&mma7455L_tq_d, (first_sampling_time * HZ)/1000);
   //disable_irq(INT1_DRDY_IRQ);

   printk("<1>[mma7455L_probe] Device has been initialized\n");

   return 0;
}

/*!
 * MMA7455L I2C detect_client function
 *
 * @param adapter            struct i2c_adapter *
 * @param address            int
 * @param kind               int
 *
 * @return  Error code indicating success or failure
 */
static int mma7455L_detect_client(struct i2c_adapter *adapter, int address,
                                 int kind)
{
	int err;
	signed char acc_buff[3];

        mma7455L_i2c_client.adapter = adapter;
#ifdef NOT_NEEDED
        err = i2c_attach_client(&mma7455L_i2c_client);
        if (err) {
                mma7455L_i2c_client.adapter = NULL;
                printk(KERN_ERR "<1>[mma7455L_detect_client] i2c_attach_client failed; err = 0x%0X\n", err);
                return err;
        }
#endif

        // Can we talk to the device?
        err = read_mma7455L_regs(ACCEL_I2CAD_REG, acc_buff, 1);
        if (err < 0) {
                mma7455L_reads_OK = -1;
                return -1;
        }

        printk(KERN_INFO "Freescale MMA7455L Accelerometer Detected\n");

        return 0;
}

/* Address to scan; addr_data points to this variable */
static unsigned short normal_i2c[] = { ACCEL_I2C_ADDRESS, I2C_CLIENT_END };

/* Magic definition of all other variables and things */
//I2C_CLIENT_INSMOD;
I2C_CLIENT_INSMOD_1(mma7455L);

/*!
 * MMA7455L I2C attach function
 *
 * @param adapter            struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
static int mma7455L_attach(struct i2c_adapter *adap)
{
        int err;

        err = i2c_probe(adap, &addr_data, mma7455L_detect_client);

        err = mma7455L_detect_client(adap, mma7455L_i2c_client.addr, 0);

        return err;
}

/*!
 * MMA7455L I2C detach function
 *
 * @param client            struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int mma7455L_detach(struct i2c_client *client)
{
        int err;

        if (!mma7455L_i2c_client.adapter)
                return -1;

        err = i2c_detach_client(&mma7455L_i2c_client);
        mma7455L_i2c_client.adapter = NULL;

        return err;
}



/*!
 * Dissociates the driver from the MMA7455L Accelerometer device.
 *
 * @param   pdev  the device structure used to give information on which SDHC
 *                to remove
 *
 * @return  The function always returns 0.
 */
static int __devexit mma7455L_remove(struct platform_device *pdev)
{
   // Stop any pending delayed work items
   cancel_delayed_work(&mma7455L_tq_d);

   // Disable IRQ lines
   disable_irq(INT1_DRDY_IRQ);

   // Put MMA7455L in standby mode
   sleep_mma7455L();

   // Release GPIO pins and IRQs
   if (int1_drdy_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(int1_drdy_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(INT1_DRDY_IRQ, NULL);
   }
   if (int2_gpio != (iomux_pin_name_t)NULL) {
      mxc_free_iomux(int2_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      free_irq(INT2_IRQ, NULL);
   }

   // Remove the /proc file
   remove_proc_entry(ACCEL_PROC_FILE, &proc_root);

   // Remove misc device entry
   misc_deregister(&mma7455L_dev);

   // Unregister i2c device
   i2c_del_driver(&mma7455L_i2c_driver);

   printk("<1>[mma7455L_exit] IRQs released and polling stopped.\n");

   return 0;
}


/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mma7455L_driver = {
        .driver = {
                   .name = "mma7455L",
                   .owner  = THIS_MODULE,
                   //.bus = &platform_bus_type,
                   },
        .suspend = mma7455L_suspend,
        .resume  = mma7455L_resume,
        .probe   = mma7455L_probe,
        .remove  = __devexit_p(mma7455L_remove),
};



/*!
 * This function is called for module initialization.
 * It registers the MMA7455 char driver.
 *
 * @return      0 on success and a non-zero value on failure.
 */
static int __init mma7455L_init(void)
{
	int err;

        err = platform_driver_register(&mma7455L_driver);
        if (err < 0) {
           platform_driver_unregister(&mma7455L_driver);
	   return err;
	}
	err = i2c_add_driver(&mma7455L_i2c_driver);
        if (err < 0) {
	   i2c_del_driver(&mma7455L_i2c_driver);
	   return err;
	}
	if (mma7455L_reads_OK < 0) {
           printk(KERN_INFO "Freescale MMA7455L Accelerometer device not found\n");
	   i2c_del_driver(&mma7455L_i2c_driver);
           platform_driver_unregister(&mma7455L_driver);
	   return -ENODEV;
	}
        mma7455L_probe(NULL);
        printk(KERN_INFO "Freescale MMA7455L Accelerometer driver loaded\n");
        return err;
}

/*!
 * This function is called whenever the module is removed from the kernel. It
 * unregisters the MMA7455 driver from kernel.
 */
static void __exit mma7455L_exit(void)
{
        mma7455L_remove(NULL);
        platform_driver_unregister(&mma7455L_driver);
}



module_init(mma7455L_init);
module_exit(mma7455L_exit);

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("Freescale MMA7455L Accelerometer Device Driver");
MODULE_LICENSE("GPL");
