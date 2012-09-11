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
#include <asm/arch/board_id.h>

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
static bool accel_locked = false;
static bool mma7660F_v1p1 = false;
static signed char calibration_regs[5];
static bool calibration_done = false;
static int x_shift = 0;
static int y_shift = 0;
static int z_shift = 0;
static int x_total = 0;
static int y_total = 0;
static int z_total = 0;
static int xyz_samples = 0;

// Proc entry structure
#define ACCEL_PROC_FILE "accelerometer"
#define PROC_CMD_LEN 50
static struct proc_dir_entry *proc_entry;

// Workqueue
static struct delayed_work mma7660F_tq_d;
static struct delayed_work mma7660F_version_tq_d;
static void detect_mma7660F_pos(struct work_struct *);
static void detect_mma7660F_version(struct work_struct *);

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
#define DEFAULT_ACCEL_SAMPLING_RATE    SR_AMSR_AM16
static unsigned char accel_sampling_rate = DEFAULT_ACCEL_SAMPLING_RATE;
#define DEFAULT_ACCEL_TILT_DEBOUNCE    SR_FILT_6
static unsigned char accel_tilt_debounce = DEFAULT_ACCEL_TILT_DEBOUNCE;

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
   LLOG_DEBUG3("Wrote 0x%02X to register 0x%02X; ret_code = 0x%02X\n", mma7660F_wr_data[0], mma7660F_reg_addr, ret_code);
   if (ret_code < 0) {
      LLOG_ERROR("write_err", "", "Attempted writing 0x%02X to MMA7660F register 0x%02X; failed with ret_code = 0x%X\n", mma7660F_wr_data[0], mma7660F_reg_addr, ret_code);
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
      LLOG_ERROR("read_fail" ,"", "Attempted reading from MMA7660F register 0x%02X; failed with ret_code = 0x%X\n", mma7660F_reg_addr, ret_code);
   }
   else {
      // Read successful
      if (debug > 3) {
         for (i=0; i<count; i++) {
            LLOG_DEBUG3("Read 0x%02X from register 0x%02X; ret_code = 0x%X\n", rd_buff[i], mma7660F_reg_addr+i, ret_code);
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
   unsigned char sample_rates = 0;

   write_mma7660F_reg(ACCEL_MODE_REG, 0); // Sleep mode so we can modify params

   // Interrupt on any position change detected
   interrupts = INTSU_FBINT | INTSU_PLINT;

   // Initialize sample rates for orientation detection
   sample_rates = accel_tilt_debounce | accel_sampling_rate; 

   // Write the MMA7660F registers with the setup data
   write_mma7660F_reg(ACCEL_INTSU_REG, interrupts); 
   write_mma7660F_reg(ACCEL_SR_REG, sample_rates); 

   // Disable pulse detection
   write_mma7660F_reg(ACCEL_PDET_REG, (SR_PDET_XDA | SR_PDET_YDA |
                      SR_PDET_ZDA)); 

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
      LLOG_WARN("orient_unknown", "", "*** Device orientation UNKNOWN\n");
      break;
   case DEVICE_UP:
      report_event(ORIENTATION_STRING_UP);
      LLOG_DEBUG0("*** Device orientation is now UP\n");
      break;
   case DEVICE_DOWN:
      report_event(ORIENTATION_STRING_DOWN);
      LLOG_DEBUG0("*** Device orientation is now DOWN\n");
      break;
   case DEVICE_LEFT:
      report_event(ORIENTATION_STRING_LEFT);
      LLOG_DEBUG0("*** Device orientation is now LEFT\n");
      break;
   case DEVICE_RIGHT:
      report_event(ORIENTATION_STRING_RIGHT);
      LLOG_DEBUG0("*** Device orientation is now RIGHT\n");
      break;
   case DEVICE_FACE_UP:
      report_event(ORIENTATION_STRING_FACE_UP);
      LLOG_DEBUG0("*** Device orientation is now FACE_UP\n");
      break;
   case DEVICE_FACE_DOWN:
      report_event(ORIENTATION_STRING_FACE_DOWN);
      LLOG_DEBUG0("*** Device orientation is now FACE_DOWN\n");
      break;
   default:
      LLOG_WARN("bad_val_orient", "", "INVALID ORIENTATION VALUE PASSED\n");
      break;
   };
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
 * Read the x,y, and z registers of the MMA7660F and determine the
 * device's orientation regardless of status of the tilt register.
 */

static int xyz_orientation(signed char *calculated_tilt)
{
   signed char acc_data[3];
   char orientation = DEVICE_UNKNOWN;
   int err;
   int timeout;

   // Read the x,y, and z registers until data is valid or timeout

   err = XOUT_ALERT;
   timeout = 10;  // Number of times to retry reading register
   while ( (err != 0) && (timeout-- > 0)) {
      err = read_mma7660F_regs(ACCEL_XOUT_REG, acc_data, 3);
      if (err >= 0) {
         err = ((acc_data[0] | acc_data[1] | acc_data[2]) & XOUT_ALERT);
      }
      schedule();  // Relax CPU use
   }
   if ((err != 0) || (timeout <= 0)) {
      LLOG_ERROR("xyz_rd_fail", "", "Could not read MMA7660F x,y,z registers.\n");
      return orientation;
   }

   // Determine orientation from x,y, and z readings
   // Note: in Tron, 32 counts equals 1.5g
#define ONE_G      21
#define POINT_25_G  5
#define POINT_80_G 17

   acc_data[0] &= XOUT_DATA_MASK;
   if (acc_data[0] >= 0x20) {
      acc_data[0] |= ~(XOUT_DATA_MASK); // Sign extension for negative
   }
   acc_data[1] &= YOUT_DATA_MASK;
   if (acc_data[1] >= 0x20) {
      acc_data[1] |= ~(YOUT_DATA_MASK); // Sign extension for negative
   }
   acc_data[2] &= ZOUT_DATA_MASK;
   if (acc_data[2] >= 0x20) {
      acc_data[2] |= ~(ZOUT_DATA_MASK); // Sign extension for negative
   }
   LLOG_DEBUG2("x = %d; y = %d; z = %d\n", acc_data[0], acc_data[1], acc_data[2]);
   // Keep statistics
   x_total += acc_data[0];
   y_total += acc_data[1];
   z_total += acc_data[2];
   xyz_samples += 1;

   // We use the same algorithm that Freescale uses to set the tilt register
   *calculated_tilt = 0;
   if ( (abs(acc_data[2]) < POINT_80_G) && 
        (abs(acc_data[0]) > abs(acc_data[1])) &&
        ( (abs(acc_data[0]) > POINT_25_G) ||
          (abs(acc_data[1]) > POINT_25_G) )  &&
        (acc_data[0] < 0)  ) {
      orientation = DEVICE_UP;
      *calculated_tilt |= TILT_POLA_UP;
      LLOG_DEBUG2("xyz logic = DEVICE_UP\n");
   }
   if ( (abs(acc_data[2]) < POINT_80_G) && 
        (abs(acc_data[0]) > abs(acc_data[1])) &&
        ( (abs(acc_data[0]) > POINT_25_G) ||
          (abs(acc_data[1]) > POINT_25_G) )  &&
        (acc_data[0] > 0)  ) {
      LLOG_DEBUG2("xyz logic = DEVICE_DOWN\n");
      *calculated_tilt |= TILT_POLA_DOWN;
      if (orientation == DEVICE_UNKNOWN) {
         orientation = DEVICE_DOWN;
      }
   }
   if ( (abs(acc_data[2]) < POINT_80_G) && 
        (abs(acc_data[1]) > abs(acc_data[0])) &&
        ( (abs(acc_data[0]) > POINT_25_G) ||
          (abs(acc_data[1]) > POINT_25_G) )  &&
        (acc_data[1] < 0)  ) {
      LLOG_DEBUG2("xyz logic = DEVICE_LEFT\n");
      *calculated_tilt |= TILT_POLA_LEFT;
      if (orientation == DEVICE_UNKNOWN) {
         orientation = DEVICE_LEFT;
      }
   }
   if ( (abs(acc_data[2]) < POINT_80_G) && 
        (abs(acc_data[1]) > abs(acc_data[0])) &&
        ( (abs(acc_data[0]) > POINT_25_G) ||
          (abs(acc_data[1]) > POINT_25_G) )  &&
        (acc_data[1] > 0)  ) {
      LLOG_DEBUG2("xyz logic = DEVICE_RIGHT\n");
      *calculated_tilt |= TILT_POLA_RIGHT;
      if (orientation == DEVICE_UNKNOWN) {
         orientation = DEVICE_RIGHT;
      }
   }
   if (acc_data[2] < -POINT_25_G) {
      LLOG_DEBUG2("xyz logic = DEVICE_FACE_UP\n");
      *calculated_tilt |= TILT_BAFR_BACK;
      if (orientation == DEVICE_UNKNOWN) {
         orientation = DEVICE_FACE_UP;
      }
   }
   if (acc_data[2] > POINT_25_G) {
      LLOG_DEBUG2("xyz logic = DEVICE_FACE_DOWN\n");
      *calculated_tilt |= TILT_BAFR_FRONT;
      if (orientation == DEVICE_UNKNOWN) {
         orientation = DEVICE_FACE_DOWN;
      }
   }

   // Adjust for rotation
   orientation = adjust_pos(orientation);

   // Correct position if MMA7660F is loaded on the back side of the board
   if (flip != 0) {
      switch (orientation) {
      case DEVICE_LEFT:
         orientation = DEVICE_RIGHT;
      break;
      case DEVICE_RIGHT:
         orientation = DEVICE_LEFT;
      break;
      case DEVICE_FACE_UP:
         orientation = DEVICE_FACE_DOWN;
      break;
      case DEVICE_FACE_DOWN:
         orientation = DEVICE_FACE_UP;
      break;
      default:
      break;
      }
   }

   return orientation;
}


/*
 * Determine if the chip version is 1.1 or later
 * The algorithm is simple: if the orientation has not been updated by the
 * time this routine is executed, we assume that the chip did not interrupt
 * after initialization; therefore it is a v1.1; otherwise it's v1.2 or later.
 */

static void detect_mma7660F_version(struct work_struct *work)
{
   if (device_orientation == DEVICE_INIT_ORIENTATION) {
      LLOG_INFO("ver_1p1", "", "Freescale MMA7660F chip detected as v1.1\n");
      mma7660F_v1p1 = true;
   }
   else {
      LLOG_INFO("ver_1p2", "", "Freescale MMA7660F chip detected as v1.2 or later\n");
      mma7660F_v1p1 = false;
   }
}


/*
 * Determine the device's orientation and report if changed.
 */

static void detect_mma7660F_pos(struct work_struct *work)
{
   char position = DEVICE_UNKNOWN;
   signed char acc_data[1];
   int err;
   signed char calculated_tilt;

   err = read_mma7660F_regs(ACCEL_TILT_REG, acc_data, 1);
   if (err >= 0) {
      LLOG_DEBUG2("Tilt register = 0x%02X\n", acc_data[0]);
      if ((acc_data[0] & TILT_SHAKE) != 0) {
         LLOG_DEBUG2("TILT says SHAKE\n");
      }
      switch (acc_data[0] & TILT_POLA) {
         case TILT_POLA_UP:
            LLOG_DEBUG2("TILT says UP\n");
            break;
         case TILT_POLA_DOWN:
            LLOG_DEBUG2("TILT says DOWN\n");
            break;
         case TILT_POLA_LEFT:
            LLOG_DEBUG2("TILT says LEFT\n");
            break;
         case TILT_POLA_RIGHT:
            LLOG_DEBUG2("TILT says RIGHT\n");
            break;
         default:
            LLOG_DEBUG2("TILT says u/d/l/r UNKNOWN\n");
            break;
      }
      switch (acc_data[0] & TILT_BAFR) {
         case TILT_BAFR_FRONT:
            LLOG_DEBUG2("TILT says FACE DOWN\n");
            break;
         case TILT_BAFR_BACK:
            LLOG_DEBUG2("TILT says FACE UP\n");
            break;
         default:
            LLOG_DEBUG2("TILT says face u/d UNKNOWN\n");
            break;
      }

   }
   else {
      LLOG_DEBUG2("Error reading tilt register\n");
   }

   if (((acc_data[0] & TILT_SHAKE) != 0) && !mma7660F_v1p1) {
      // Shake bit is set; if chip is not v1.1, do not check or report
      // orientation and reset MMA7660F to clear the tilt register
      write_mma7660F_reg(ACCEL_MODE_REG, 0); // Sleep mode
      LLOG_DEBUG0("Resetting mma7660F because shake bit was set.\n");
      write_mma7660F_reg(ACCEL_MODE_REG, MODE_ACTIVE); 
   }
   else {
      position = xyz_orientation(&calculated_tilt);
      LLOG_DEBUG2("calculated_tilt = 0x%02X\n", calculated_tilt);
      if (((acc_data[0] & (TILT_BAFR | TILT_POLA)) != calculated_tilt) &&
          !mma7660F_v1p1) {
         // Calculated tilt does not correspond to value from tilt register;
         // If chip is not v1.1, reset it to clear the tilt register
         // and do not report orientation.
         write_mma7660F_reg(ACCEL_MODE_REG, 0); // Sleep mode
         LLOG_DEBUG0("Resetting mma7660F because TILT and calculated_tilt did not match.\n");
         write_mma7660F_reg(ACCEL_MODE_REG, MODE_ACTIVE); 
      }
      else if (device_orientation != position) {
         // If chip is not v1.1, reset it to clear the tilt register
         if (!mma7660F_v1p1) {
            write_mma7660F_reg(ACCEL_MODE_REG, 0); // Sleep mode
         }
         // Report new orientation
         device_orientation = position;
         report_orientation(device_orientation);
         if (!mma7660F_v1p1) {
            write_mma7660F_reg(ACCEL_MODE_REG, MODE_ACTIVE); 
         }
      }
      else {
         LLOG_DEBUG0("Not reporting device orientation because it did not change.\n");
      }
   }
}


/*
 * This IRQ routine is executed when the INT line is asserted,
 * indicating that data is ready for sampling.
 */

static irqreturn_t mma7660F_irq (int irq, void *data, struct pt_regs *r)
{
   LLOG_DEBUG2("Received INT IRQ from mma7660F.\n");

   // Schedule work function to read device status.
   schedule_delayed_work(&mma7660F_tq_d, 0);

   return IRQ_HANDLED;
}


/*!
 * This function calibrates the mma7660F version 1.2 found in Nell DVT
 * units.  It should not be called for any other versions.
 */
static int mma7660F_calibrate(int delta_x, int delta_y, int delta_z)
{
   signed char acc_data[5];
   int err = 0;
   int x_offset, y_offset, z_offset;
   int count;

   // Put device in factory test mode
   err |= write_mma7660F_reg(ACCEL_MODE_REG, 0);
   err |= write_mma7660F_reg(0x20, 0x01);
   err |= write_mma7660F_reg(0x20, 0x02);
   err |= read_mma7660F_regs(0x20, acc_data, 1);
   err |= (acc_data[0] != 0x03);

   // Put device in active mode and read offset registers
   err |= write_mma7660F_reg(ACCEL_MODE_REG, 0x01);
   err |= read_mma7660F_regs(0x31, acc_data, 5);

   // If new offset register values have not been previously calculated, do it

   if (!calibration_done) {
      x_offset = ((acc_data[0] >> 7) & 0x0001) | (acc_data[1] << 1) |
                 ((acc_data[2] & 0x01) << 9);
      LLOG_DEBUG2("x offset read = %d\n", x_offset);
      if ((acc_data[2] & 0x02) != 0) {
         x_offset = -x_offset;
      }
      LLOG_DEBUG2("x offset after sign check = %d\n", x_offset);
      x_offset += (delta_x * (-2));
      LLOG_DEBUG2("x offset after shift added = %d\n", x_offset);
      if (x_offset < 0) {
         x_offset = (-(x_offset)) | 0x0400;
      }
      LLOG_DEBUG2("x offset after shift added and sign checked = %d\n", x_offset);
   
      y_offset = ((acc_data[2] >> 2) & 0x003F) | ((acc_data[3] & 0x0F) << 6);
      LLOG_DEBUG2("y offset read = %d\n", y_offset);
      if ((acc_data[3] & 0x10) != 0) {
         y_offset = -y_offset;
      }
      LLOG_DEBUG2("y offset after sign check = %d\n", y_offset);
      y_offset += (delta_y * (-2));
      LLOG_DEBUG2("y offset after shift added = %d\n", y_offset);
      if (y_offset < 0) {
         y_offset = (-(y_offset)) | 0x0400;
      }
      LLOG_DEBUG2("y offset after shift added and sign checked = %d\n", y_offset);
   
      z_offset = ((acc_data[3] >> 5) & 0x0007) | ((acc_data[4] & 0x7F) << 3);
      LLOG_DEBUG2("z offset read = %d\n", z_offset);
      if ((acc_data[4] & 0x80) != 0) {
         z_offset = -z_offset;
      }
      LLOG_DEBUG2("z offset after sign check = %d\n", z_offset);
      z_offset += (delta_z * (-2));
      LLOG_DEBUG2("z offset after shift added = %d\n", z_offset);
      if (z_offset < 0) {
         z_offset = (-(z_offset)) | 0x0400;
      }
      LLOG_DEBUG2("z offset after shift added and sign checked = %d\n", z_offset);

      calibration_regs[0] = (acc_data[0] & 0x7F) | (((unsigned char)(x_offset & 0x0001)) << 7);
      calibration_regs[1] = (unsigned char)((x_offset & 0x01FE) >> 1);
      calibration_regs[2] = (unsigned char)((x_offset & 0x0600) >> 9) |
                    (((unsigned char)(y_offset & 0x003F)) << 2);
      calibration_regs[3] = (unsigned char)((y_offset & 0x07C0) >> 6) |
                    (((unsigned char)(z_offset & 0x0007)) << 5);
      calibration_regs[4] = (unsigned char)((z_offset & 0x07F8) >> 3);

      calibration_done = true;
   }
   else {
      LLOG_DEBUG2("Using previously calculated calibration register values\n");
   }

   // Write new offset register values only if different from values read
   for (count = 0; count < 5; count++) {
      if (acc_data[count] != calibration_regs[count]) {
         err |= write_mma7660F_reg(0x22, 0x01);
         err |= write_mma7660F_reg((count+0x31), calibration_regs[count]);
         err |= write_mma7660F_reg(0x22, 0x00);
      }
   }

   return err;

}


/*!
 * This function puts the MMA7660F device in low-power mode/state,
 * and it disables interrupts from the input line.
 */
void mma7660F_sleep(void)
{
   if (!accel_locked) {
      // Only perform locking functions if accelerometer is not locked
      LLOG_DEBUG1("Disabling IRQ and putting accelerometer in standby mode...\n");

      // Disable IRQ line
      disable_irq(INT_IRQ);

      // Stop any pending delayed work items
      cancel_rearming_delayed_work(&mma7660F_tq_d);
      cancel_rearming_delayed_work(&mma7660F_version_tq_d);

      // Put Freescale MMA7660F Accelerometer in standby mode
      write_mma7660F_reg(ACCEL_MODE_REG, 0);

      accel_locked = true;
   }
}


/*!
 * This function brings the MMA7660F device back from low-power state,
 * and re-enables the line interrupt.
 */
void mma7660F_wake(void)
{
   if (accel_locked) {
      // Only perform locking functions if accelerometer is locked
      LLOG_DEBUG1("Enabling IRQ and waking mma7660F\n");

      // Enable IRQ line
      enable_irq(INT_IRQ);

      // Initialize the MMA7660F; an interrupt will be generated when
      // the first measurement is available
      init_mma7660F();

      accel_locked = false;
   }
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
   int orientation;
   signed char calculated_tilt;

   if (off > 0) {
     *eof = 1;
     return 0;
   }

   orientation = xyz_orientation(&calculated_tilt);

   switch (orientation) {
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
   unsigned int new_hold_time;
   unsigned int new_sample_rate;
   unsigned int calculated_debounce;
   char *x_sign = " ";
   char *y_sign = " ";
   char *z_sign = " ";
   int x_rnd, y_rnd, z_rnd;

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
   else if ( !strncmp(command, "calibrate", 9) ) {
      calibration_done = false; // Change flag since we're forcing calibration
      sscanf(command, "calibrate %d,%d,%d", &x_shift, &y_shift, &z_shift);
      err = mma7660F_calibrate(x_shift, y_shift, z_shift);
      if (err == 0) {
         LLOG_INFO("calibrated", "", "MMA7660F calibrated\n");
      }
      else {
         LLOG_ERROR("not_calibrated", "", "Calibration failed!\n");
      }
   }
   else if ( !strncmp(command, "xyz_start", 9) ) {
      // Start xyz data collection
      LLOG_INFO("xyz_start", "", "starting X,Y,Z data collection\n");
      write_mma7660F_reg(ACCEL_MODE_REG, 0);  // Standby MMA7660F
      write_mma7660F_reg(ACCEL_INTSU_REG, 0x10);  // Interrupt at each measr.
      x_total = 0; y_total = 0; z_total = 0; xyz_samples = 0;
      write_mma7660F_reg(ACCEL_MODE_REG, 0x01);  // Wake up MMA7660F
   }
   else if ( !strncmp(command, "xyz_end", 7) ) {
      // End xyz data collection and report results
      write_mma7660F_reg(ACCEL_MODE_REG, 0);  // Standby MMA7660F
      if (xyz_samples == 0) { // No samples taken; we don't want to divide by 0
         LLOG_WARN("xyz_zero", "", "stopped X,Y,Z data collection. ***ZERO*** samples taken. X,Y,Z averages = 0.0,0.0,0.0\n");
         LLOG_WARN("xyz_rounded_zero", "", "***ZERO*** X,Y,Z rounded averages = 0,0,0\n");
      }
      else {
         if ( (x_total < 0) && ( (x_total/xyz_samples) == 0) ) {
            x_sign = "-";
         }
         else {
            x_sign = "";
         }
         if ( (y_total < 0) && ( (y_total/xyz_samples) == 0) ) {
            y_sign = "-";
         }
         else {
            y_sign = "";
         }
         if ( (z_total < 0) && ( (z_total/xyz_samples) == 0) ) {
            z_sign = "-";
         }
         else {
            z_sign = "";
         }
         LLOG_INFO("xyz_end", "", "stopped X,Y,Z data collection. %d samples taken. X,Y,Z averages = %s%1d.%1d,%s%1d.%1d,%s%1d.%1d\n", xyz_samples, x_sign, x_total/xyz_samples, (abs(x_total) % xyz_samples)*10/xyz_samples, y_sign, y_total/xyz_samples, (abs(y_total) % xyz_samples)*10/xyz_samples, z_sign, z_total/xyz_samples, (abs(z_total) % xyz_samples)*10/xyz_samples);
         if ( abs(x_total/xyz_samples) <= 10 ) {
            // Normal rounding
            x_rnd = (x_total+(x_total > 0 ? 1 : -1)*(xyz_samples/2))/xyz_samples;
         }
         else {
            // Rounding towards 21.3 counts, which is 1g
            x_rnd = (x_total+(x_total > 0 ? 1 : -1)*(xyz_samples/5))/xyz_samples;
         }
         if ( abs(y_total/xyz_samples) <= 10 ) {
            // Normal rounding
            y_rnd = (y_total+(y_total > 0 ? 1 : -1)*(xyz_samples/2))/xyz_samples;
         }
         else {
            // Rounding towards 21.3 counts, which is 1g
            y_rnd = (y_total+(y_total > 0 ? 1 : -1)*(xyz_samples/5))/xyz_samples;
         }
         if ( abs(z_total/xyz_samples) <= 10 ) {
            // Normal rounding
            z_rnd = (z_total+(z_total > 0 ? 1 : -1)*(xyz_samples/2))/xyz_samples;
         }
         else {
            // Rounding towards 21.3 counts, which is 1g
            z_rnd = (z_total+(z_total > 0 ? 1 : -1)*(xyz_samples/5))/xyz_samples;
         }
         LLOG_INFO("xyz_rounded", "", "X,Y,Z rounded averages = %d,%d,%d\n", x_rnd, y_rnd, z_rnd);
      }
      init_mma7660F();  // Re-start MMA7660F in normal mode of operation
   }
   else if ( !strncmp(command, "sample", 6) ) {
      // Requested to change sampling rate
      sscanf(command, "sample %d", &new_sample_rate);
      switch (new_sample_rate) {
      case 1:
         accel_sampling_rate = SR_AMSR_AM1;
         break;
      case 2:
         accel_sampling_rate = SR_AMSR_AM2;
         break;
      case 4:
         accel_sampling_rate = SR_AMSR_AM4;
         break;
      case 8:
         accel_sampling_rate = SR_AMSR_AM8;
         break;
      case 16:
         accel_sampling_rate = SR_AMSR_AM16;
         break;
      case 32:
         accel_sampling_rate = SR_AMSR_AM32;
         break;
      case 64:
         accel_sampling_rate = SR_AMSR_AM64;
         break;
      default:
         LLOG_WARN("bad_sample", "", "MMA7660F sampling rate value %d is not allowed; valid values are: 1,2,4,8,16,32, and 64\n", new_sample_rate);
         return(len);
         break;
      }
      init_mma7660F();  // Update mma7660F registers with new values
      LLOG_INFO("sample", "", "New MMA7660F sampling rate set to %d samples/sec\n", new_sample_rate);
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

   // Initialize the work items
   INIT_DELAYED_WORK(&mma7660F_tq_d, detect_mma7660F_pos);
   INIT_DELAYED_WORK(&mma7660F_version_tq_d, detect_mma7660F_version);

   // Register the device file
   error = misc_register(&mma7660F_dev);
   if (error) {
      LLOG_ERROR("reg_dev", "", "failed to register device mma7660F_dev\n");
      return error;
   }

   // Initialize semaphore 
   accel_locked = false;

   // Initialize the MMA7660F; an interrupt will be generated when
   // the first measurement is available, if the chip is rev 1.2 or later
   init_mma7660F();

   // Assume that the MMA7660F is a version later than 1.1, and schedule
   // a work routine to execute in 1 sec.  If the accelerometer has not
   // interrupted with a measurement by then, we assume that it is v1.1
   // The implications are that on v1.1 we will not reset the MMA7660F
   // during normal operation because this version of the chip does not
   // generate interrupts when the orientation goes from "unknown" to "known"
   mma7660F_v1p1 = false;
   schedule_delayed_work(&mma7660F_version_tq_d, (HZ*1));

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
