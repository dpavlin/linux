/*
** Haptic device driver routine for Mario.  (C) 2008 Lab126
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
#include <linux/spinlock.h>

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

#include "haptic.h"

// For direct CPU register access
typedef u32 * volatile reg_addr_t;
static uint32_t reg_addr, reg_data;
#define WR_MXC_REG(addr, val)   reg_addr=addr; __raw_writel(val, reg_addr);
#define RD_MXC_REG(addr)   reg_addr=addr; reg_data=__raw_readl(reg_addr); 
#define MOD_MXC_REG(addr, val, mask) RD_MXC_REG(addr); reg_data &= ~mask; reg_data |= val; WR_MXC_REG(reg_addr, reg_data);
#define DISABLE_PWM      MOD_MXC_REG(PWMCR, PWMCR_DISABLE_PWM, PWMCR_EN_MASK);
#define ENABLE_PWM       MOD_MXC_REG(PWMCR, PWMCR_ENABLE_PWM, PWMCR_EN_MASK);
#define DISABLE_FE_INT   MOD_MXC_REG(PWMIR, PWMIR_FIE_DISABLE, PWMIR_FIE_MASK);
#define ENABLE_FE_INT    MOD_MXC_REG(PWMIR, PWMIR_FIE_ENABLE, PWMIR_FIE_MASK);
#define DISABLE_PWM_CLK  MOD_MXC_REG(CCM_CGR1, CCM_CGR1_DISABLE_PWM_CLK, CCM_CGR1_PWM_CLK_MASK);
#define ENABLE_PWM_CLK  MOD_MXC_REG(CCM_CGR1, CCM_CGR1_ENABLE_PWM_CLK, CCM_CGR1_PWM_CLK_MASK);
#define DISCONNECT_PWM_OUTPUT  MOD_MXC_REG(PWMCR, PWMCR_DISCONN_OUTPUT, PWMCR_POUTC_MASK);
#define ENABLE_PWM_OUTPUT  MOD_MXC_REG(PWMCR, PWMCR_SET_THEN_CLR, PWM_POUTC_MASK);

// Input device structure
static struct miscdevice haptic_dev = { MISC_DYNAMIC_MINOR,
                                        "haptic",
                                        NULL,
                                      };
                         

// Global variables

static struct timer_list haptic_timer;
static int haptic_state = HAPTIC_DEV_OFF;
static long pwm_cycles_to_go = 0;  
static uint32_t pwm_sample_val = 0;
DEFINE_SPINLOCK(pwm_lock);

// Proc entry structure and global variable.
#define HAPTIC_PROC_FILE "haptic"
#define PROC_CMD_LEN 1024
static struct proc_dir_entry *proc_entry;
static int haptic_lock = 0;  // 0=unlocked; non-zero=locked

// The following array contains information for the haptic device
// line for each different hardware revision.
// A NULL value in any element indicates that the line is not a GPIO
static iomux_pin_name_t line_info[MAX_VERSIONS];

static iomux_pin_name_t haptic_gpio;


// Module parameters

#define SCROLL_REV_DEFAULT 0
static int scroll_rev = SCROLL_REV_DEFAULT;
module_param(scroll_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(scroll_rev, "Device hardware revision; 0=Mario, 1=Turing prototype build, 2=Turing v1 (default is 0)");

#define HAPTIC_REV_DEFAULT 1
static int haptic_rev = HAPTIC_REV_DEFAULT;
module_param(haptic_rev, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_rev, "Haptic hardware revision; 0=Turing EVT1 and Nell Prototype, 1=Turing EVT2, Nell EVT1 and later (default is 1)");

// Haptic feedback device parameters

#define HAPTIC_TIMEOUT_DEFAULT 10
#define HAPTIC_PULSE_WIDTH_DEFAULT 3200
#define HAPTIC_PWM_DUTY_CYCLE_DEFAULT 50
#define HAPTIC_PWM_FREQ_DEFAULT 33000
#define HAPTIC_COUNT_DEFAULT 1

static int haptic_time = HAPTIC_TIMEOUT_DEFAULT;
module_param(haptic_time, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_time, "Time between haptic device signal changes, in msec; must be a multiple of 10 (default is 10)");

static int haptic_pulse_width = HAPTIC_PULSE_WIDTH_DEFAULT;
module_param(haptic_pulse_width, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_pulse_width, "Width of active portion of PWM-generated haptic signal, in micro-seconds (default is 3200)");

static int haptic_pwm_duty_cycle = HAPTIC_PWM_DUTY_CYCLE_DEFAULT;
module_param(haptic_pwm_duty_cycle, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_pwm_duty_cycle, "Duty cycle for each pulse within the active portion of PWM-generated haptic signal, in percent; value MUST be 0-100 (default is 50)");

static int haptic_pwm_freq = HAPTIC_PWM_FREQ_DEFAULT;
module_param(haptic_pwm_freq, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_pwm_freq, "Frequency for PWM-generated pulses, in Hz (default is 33000)");

static int haptic_count_default = HAPTIC_COUNT_DEFAULT;
module_param(haptic_count_default, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(haptic_count_default, "Number of pulses sent to the haptic device; 0=no haptic feedback (default is 1)");

#define VVIB_VOLTAGE_DEFAULT   VVIB_3V
static int vvib_voltage = VVIB_VOLTAGE_DEFAULT;
module_param(vvib_voltage, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(vvib_voltage, "Voltage for haptic device supply; 0=1.3V, 1=1.8V, 2=2V, 3=3V (default is 3)");

static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging; 0=off, any other value=on (default is off)");


/*
 * Print all the PWM registers, for debugging
 */
void dump_pwm_regs(void)
{
    RD_MXC_REG(PWMCR);
    printk("PWMCR  = 0x%08X\n", reg_data);
    RD_MXC_REG(PWMSR);
    printk("PWMSR  = 0x%08X\n", reg_data);
    RD_MXC_REG(PWMIR);
    printk("PWMIR  = 0x%08X\n", reg_data);
    RD_MXC_REG(PWMSAR);
    printk("PWMSAR  = 0x%08X\n", reg_data);
    RD_MXC_REG(PWMPR);
    printk("PWMPR  = 0x%08X\n", reg_data);
    RD_MXC_REG(PWMCNR);
    printk("PWMCNR  = 0x%08X\n", reg_data);
}

/*
 * Initialize the PWM unit with the proper values but don't start a haptic
 * event (ie, no PWM output signal yet)
 */
void init_pwm_regs(void)
{
   unsigned int prescale;
   unsigned int counter_freq;
   unsigned int period;
   unsigned long lock_flags;

   WR_MXC_REG(PWMCR, PWMCR_DISABLE_PWM | PWMCR_IPG_CLK_HIGHFREQ | 
                     PWMCR_SET_THEN_CLR | PWMCR_WAITEN_ON | PWMCR_FWM_4);
   if(haptic_pwm_freq && (haptic_pwm_freq < PWM_SOURCE_FREQ))
   {
      // All is OK
   }
   else {
      printk(KERN_INFO "WARNING: haptic_pwm_freq out of range; resetting to default value of %d", HAPTIC_PWM_FREQ_DEFAULT);
      haptic_pwm_freq = HAPTIC_PWM_FREQ_DEFAULT;
   }
   spin_lock_irqsave(&pwm_lock, lock_flags);

   // ipg clock input is 66.5MHz
   // Prescaler is 12-bit
   // Counter is 16-bit
   prescale =  (PWM_SOURCE_FREQ / (PWM_COUNT_RANGE * haptic_pwm_freq)) + 1;
   counter_freq = PWM_SOURCE_FREQ / prescale;
   period = counter_freq / haptic_pwm_freq;

   if (debug > 2) {
      printk(KERN_INFO "PWM: haptic_pwm_freq: %d, ps: %d, count_freq: %d, prd: %d, real out_freq: %d\n", haptic_pwm_freq, prescale, counter_freq, period, counter_freq/period );
   }

   prescale = (prescale-1) & 0xFFF;
   WR_MXC_REG(PWMPR, period);
   MOD_MXC_REG(PWMCR, (prescale << PWMCR_PRESCALER_SHIFT), 
               PWMCR_PRESCALER_MASK);
   // Duty cycle is expressed as an integer percent from 0 to 100
   if ((haptic_pwm_duty_cycle < 0) || (haptic_pwm_duty_cycle > 100)) {
      // Invalid value given; revert to default value
      printk(KERN_INFO "WARNING: haptic_pwm_duty_cyle out of range; resetting to default value of %d", HAPTIC_PWM_DUTY_CYCLE_DEFAULT);
      haptic_pwm_duty_cycle = HAPTIC_PWM_DUTY_CYCLE_DEFAULT;
   }
   if (haptic_pwm_duty_cycle == 0) {
      // Requested "quiet" period
      pwm_sample_val = period * 1 / 100;  // Do a 1% duty cycle to minimize IRQs
      DISCONNECT_PWM_OUTPUT;  // Turn off PWM output driver
   }
   else {
      // Non-zero duty cycle - output PWM signal
      pwm_sample_val = period * haptic_pwm_duty_cycle / 100;
   }
   WR_MXC_REG(PWMSAR, pwm_sample_val);

   spin_unlock_irqrestore(&pwm_lock, lock_flags);
}


/*
 * Update the PWM registers according to the number of pulses left in
 * the current cycle
 * To count pulses, we manipulate two fields:
 * PWMSAR - is the input to the FIFO; we can write up to 4 words
 * PWMCR->REPEAT - repeat use of each FIFO entry 1,2,4, or 8 times
 */
void update_pwm_regs(void)
{
   uint32_t fifo_count, repeat;
   int repeat_prn;

   if (debug > 3) {
      printk("[update_pwm_regs] %d cycles to go...\n", (int) pwm_cycles_to_go);
   }

   // Calculate new values for PWM fields being updated
   fifo_count = pwm_cycles_to_go / 8;
   if (fifo_count > 4) {
      fifo_count = 4;
      repeat = PWMCR_REPEAT_8;
      pwm_cycles_to_go -= fifo_count * 8;
   }
   else if (fifo_count > 0) {
      repeat = PWMCR_REPEAT_8;
      pwm_cycles_to_go -= fifo_count * 8;
   }
   else if (pwm_cycles_to_go > 0) {
      // Less than 8 cycles to go
      fifo_count = 1;
      if (pwm_cycles_to_go >= 4) {
         repeat = PWMCR_REPEAT_4;
         pwm_cycles_to_go -= 4;
      }
      else if (pwm_cycles_to_go >= 2) {
         repeat = PWMCR_REPEAT_2;
         pwm_cycles_to_go -= 2;
      }
      else {
         repeat = PWMCR_REPEAT_1;
         pwm_cycles_to_go -= 1;
      }
   }
   else {
      // 0 cycles to go; done with PWM cycles.  Disable PWM and exit.
      DISABLE_PWM;
      DISABLE_FE_INT;
      DISABLE_PWM_CLK;
      haptic_state = HAPTIC_AVAILABLE;
      return;
   }

   if (debug > 3) {
      switch(repeat) {
      case PWMCR_REPEAT_1:
         repeat_prn = 1;
      break;
      case PWMCR_REPEAT_2:
         repeat_prn = 2;
      break;
      case PWMCR_REPEAT_4:
         repeat_prn = 4;
      break;
      case PWMCR_REPEAT_8:
         repeat_prn = 8;
      break;
      default:
         repeat_prn = -1;
      break;
      }
      printk("[update_pwm_regs] fifo_count = %d, repeat = %d, pwm_cycles_to_go = %d\n", fifo_count, repeat_prn, (int) pwm_cycles_to_go);
   }

   // Subtract data already in FIFO
   RD_MXC_REG(PWMSR);
   fifo_count -= (reg_data & PWMSR_FIFOAV_MASK);
   if ((fifo_count < 0) || (fifo_count > PWMSR_FIFOAV_FULL)) {
      fifo_count = 0;
      printk("[update_pwm_regs] Invalid FIFO count!\n");
   }

   // Update PWM registers
   while (fifo_count > 0) {
      RD_MXC_REG(PWMSR);
      if ((reg_data & PWMSR_FIFOAV_MASK) < PWMSR_FIFOAV_FULL) {
         if (debug > 3) {
            printk("[update_pwm_regs] fifo_count = 0x%X\n", fifo_count);
         }
         WR_MXC_REG(PWMSAR, pwm_sample_val);
      }
      else {
         printk("FIFO full\n");
      }
      fifo_count--;
   }
   MOD_MXC_REG(PWMCR, repeat, PWMCR_REPEAT_MASK);

   MOD_MXC_REG(PWMSR, PWMSR_FE_CLR, PWMSR_FE_MASK);  // Clear FIFO empty bit
}


/*
 * Process interrupts from the PWM FIFO empty condition
 */

static irqreturn_t pwm_irq_handler (int irq, void *data, struct pt_regs *r)
{
   if (debug > 3) {
      printk("[pwm_irq_handler] Got here...\n");
   }

   // Update PWM count registers; PWM will be disabled if we are done
   update_pwm_regs();
   if (debug > 3) {
      printk("[pwm_irq_handler] Exiting...\n");
   }
   return IRQ_HANDLED;
}


static void haptic_timer_timeout(unsigned long timer_data)
{
   int haptic_count;
   u32 haptic_bit;

   haptic_count = haptic_timer.data;
   // Toggle the haptic device's GPIO line
   haptic_bit = mxc_get_gpio_datain(haptic_gpio);
   if (debug > 1) {
      printk("[haptic_timer_timeout] haptic_count = %d; haptic_bit = %d\n", haptic_count, haptic_bit);
   }
   haptic_bit = (~haptic_bit) & 0x01;
   mxc_set_gpio_dataout(haptic_gpio, haptic_bit);

   // Update count and stop if zero
   haptic_count--;
   if (haptic_count > 0) {
      // Restart haptic timer
      // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
      del_timer(&haptic_timer);
      haptic_timer.expires = jiffies + ((haptic_time * HZ) / 1000);
      add_timer(&haptic_timer);
      haptic_timer.data = haptic_count; // Update count for this haptic event
   }
   else {
      // Done providing haptic feedback.  Make sure haptic device FET is off!
      mxc_set_gpio_dataout(haptic_gpio, HAPTIC_OFF);
      haptic_state = HAPTIC_AVAILABLE;
   }
}


/*
 * Start the haptic device feedback mechanism
 * Returns: HAPTIC_OK if successful
 *          HAPTIC_BUSY if we are busy sending a pulse
 *          HAPTIC_DEV_OFF if the driver is suspended
 *          HAPTIC_ZERO_COUNT if count is 0 or negative
 */
int do_haptic(int count)
{
   if (debug > 2) {
      printk("[do_haptic] Starting...\n");
   }
   // Do not process request if we are busy sending a haptic event
   if ((haptic_state == HAPTIC_BUSY) || (haptic_state == HAPTIC_DEV_OFF)) {
      return haptic_state;
   }
   if (count < 1) {
      haptic_state = HAPTIC_ZERO_COUNT;
      return haptic_state;  // Haptic function is disabled if count is 0 or negative
   }

   if (haptic_rev == 0) {
      // GPIO version
      // Start haptic timer
      // Convert from ms to jiffies; in Mario HZ=100, so each jiffy is 10ms
      del_timer(&haptic_timer);
      haptic_timer.expires = jiffies + ((haptic_time * HZ) / 1000);
      add_timer(&haptic_timer);
      // Update count for this haptic event; multiply by 2 because each
      // pulse consists of a rising edge and a falling edge, which is what
      // is counted by haptic_timer_timeout
      haptic_timer.data = count * 2; 
      haptic_state = HAPTIC_BUSY;
   }
   else {
      // PWM version; sends one event at a time only; to do multiple
      // pulses, call repeatedly.  For a quiet (low) period, call with
      // a duty cycle of 1 (0 is an invalid value).

      // Calculate how many PWM cycles are contained in each haptic pulse
      // event.  We divide by 1,000,000 because the pulse width is expressed
      // in microseconds, and the PWM frequency in Hz.
      pwm_cycles_to_go = (haptic_pulse_width * haptic_pwm_freq) / 1000000;
      ENABLE_PWM_CLK;  // Start PWM input clock
      init_pwm_regs();  // Prepare PWM for operation
      update_pwm_regs();  // Set up count-related PWM registers
      ENABLE_PWM;  // Start PWM output
      ENABLE_FE_INT; // Enable FIFO-Empty interrupt generation at PWM
      haptic_state = HAPTIC_BUSY;
   }
   return HAPTIC_OK;
}


/*
 * Send haptic pulses, depending on global variables set
 */
int do_thump(void) {
   int  result = 0;
   int  count;
   int  saved_pwm_freq;
   int  saved_pwm_duty_cycle;
   int  haptic_timeout;

   if (haptic_rev == 0) {
      // GPIO version
      result = do_haptic(haptic_count_default);
   }
   else {
      // PWM version
      // Send the requested number of pulses
      count = haptic_count_default;
      while (count-- > 0) {
         // Send active half of the haptic event
         haptic_timeout = HAPTIC_WAIT_TIMEOUT;
         result = do_haptic(1);
         while((result == HAPTIC_BUSY) && (haptic_timeout-- > 0)) {
            // Retry if haptic driver was busy
            result = do_haptic(1);
         }
         if (haptic_timeout <= 0) {
            printk(KERN_INFO "WARNING: timed out waiting for haptic driver to be ready; try locking and unlocking the driver\n");
         }
         // Save PWM frequency and duty cycle
         saved_pwm_freq = haptic_pwm_freq;
         saved_pwm_duty_cycle = haptic_pwm_duty_cycle;
         // Set PWM frequency and duty cycle for inactive half
         haptic_pwm_freq = 1010000/haptic_pulse_width;
         haptic_pwm_duty_cycle = 0;
         // Wait for active half of pulse to be sent, then send
         // inactive half
         haptic_timeout = HAPTIC_WAIT_TIMEOUT;
         result = do_haptic(1);
         while((result == HAPTIC_BUSY) && (haptic_timeout-- > 0)) {
            result = do_haptic(1);
         }
         if (haptic_timeout <= 0) {
            printk(KERN_INFO "WARNING: timed out waiting for haptic driver to be ready; try locking and unlocking the driver\n");
         }
         // Restore PWM frequency and duty cycle
         haptic_pwm_freq = saved_pwm_freq;
         haptic_pwm_duty_cycle = saved_pwm_duty_cycle;
      }
   }
   return result;
}


/*!
 * This function puts the haptic device driver in low-power mode/state.
 */
void haptic_off(void)
{
   if (debug != 0) {
      printk("<1> [haptic_off] Stopping timers; disabling IRQs and pullups...\n");
   }

   // Disable IRQ lines and turn off pullups on GPIO output lines
   if (haptic_rev == 0) {
      if (haptic_timer.data != 0) {
         del_timer(&haptic_timer);
         haptic_timer.data = 0;
      }
      if (haptic_gpio != (iomux_pin_name_t)NULL) {
         // Make sure haptic device supply and FET are off for low-power suspend
         pmic_power_regulator_off(REGU_VVIB);
         mxc_set_gpio_dataout(haptic_gpio, HAPTIC_OFF);
      }
   }
   else {
      // Make sure haptic device supply and FET are off for low-power suspend
      pmic_power_regulator_off(REGU_VVIB);
      DISABLE_PWM;  // Turn off PWM output
      DISABLE_PWM_CLK;  // Turn off PWM clock
      haptic_state = HAPTIC_DEV_OFF;
   }
}


/*!
 * This function puts the haptic device driver in low-power mode/state,
 * @param   pdev  the device structure used to give information on haptic
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function always returns 0.
 */
static int haptic_suspend(struct platform_device *pdev, pm_message_t state)
{
   haptic_off();

   return 0;
}


/*!
 * This function brings the haptic device driver back from low-power state,
 * by re-enabling the line interrupts.
 *
 * @return  The function always returns 0.
 */
void haptic_on(void)
{
   if (debug != 0) {
      printk("<1> [haptic_on] Enabling haptic device...\n");
   }

   if (haptic_rev == 0) {
      if (haptic_gpio != (iomux_pin_name_t)NULL) {
         // Make sure haptic device FET is off and supply voltage is ON
         pmic_power_regulator_on(REGU_VVIB);
         mxc_set_gpio_dataout(haptic_gpio, HAPTIC_OFF);
      }
   }
   else {
      // Make sure haptic device FET is off and supply voltage is ON
      pmic_power_regulator_on(REGU_VVIB);
      DISABLE_PWM;  // Make sure PWM is disabled
      DISABLE_PWM_CLK;  // Turn off PWM clock
      haptic_state = HAPTIC_AVAILABLE;
   }
}


/*!
 * This function brings the haptic device driver back from low-power state.
 *
 * @param   pdev  the device structure used to give information to resume
 *
 * @return  The function always returns 0.
 */
static int haptic_resume(struct platform_device *pdev)
{
   haptic_on();

   return 0;
}


/*
 * This function returns the current state of the haptic device
 */
int haptic_proc_read( char *page, char **start, off_t off,
                      int count, int *eof, void *data )
{
   int len;

   if (off > 0) {
     *eof = 1;
     return 0;
   }
   if (haptic_lock == 0) {
      len = sprintf(page, "haptic is unlocked\n");
   }
   else {
      len = sprintf(page, "haptic is locked\n");
   }
   return len;
}


/*
 * This function executes haptic control commands based on the input string.
 *    unlock = unlock the haptic
 *    lock   = lock the haptic
 *    thump  = send haptic output pulse(s)
 *    signature = send a sequence of pulses
 */
ssize_t haptic_proc_write( struct file *filp, const char __user *buff,
                            unsigned long len, void *data )

{
   char command[PROC_CMD_LEN];
   char signature_str[PROC_CMD_LEN];
   char *token;
   int  keycode;
   int  result = 0;
   int  saved_count;
   int  saved_pwm_duty_cycle;

   if (len > PROC_CMD_LEN) {
      printk(KERN_ERR "haptic: command is too long!\n");
      return -ENOSPC;
   }

   if (copy_from_user(command, buff, len)) {
      return -EFAULT;
   }

   if ( !strncmp(command, "unlock", 6) ) {
      // Requested to unlock haptic
      if (haptic_lock == 0) {
         // Haptic was already unlocked; do nothing
         printk(KERN_INFO "The haptic device was already unlocked!\n");
      }
      else {
         // Unlock haptic driver
         haptic_on();
         haptic_lock = 0;
         printk(KERN_INFO "The haptic device is now unlocked\n");
      }
   }
   else if ( !strncmp(command, "lock", 4) ) {
      // Requested to lock haptic
      if (haptic_lock != 0) {
         // Haptic was already locked; do nothing
         printk(KERN_INFO "The haptic device was already locked!\n");
      }
      else {
         // Lock haptic
         haptic_off();
         haptic_lock = 1;
         printk(KERN_INFO "The haptic device is now locked\n");
      }
   }
   else if ( !strncmp(command, "thump", 5) ) {
      // Requested to provide haptic feedback pulse(s)
      result = do_thump();

      switch (result) {
      case HAPTIC_DEV_OFF:
         printk(KERN_INFO "WARNING: the driver is suspended; no haptic pulse is being sent.\n");
         break;
      case HAPTIC_ZERO_COUNT:
         printk(KERN_INFO "WARNING: haptic_count_default is zero or negative; no haptic pulse is being sent.\n");
         break;
      case HAPTIC_BUSY:
         printk(KERN_INFO "WARNING: haptic device is busy; no additional haptic pulse is being sent.\n");
         break;
      case HAPTIC_OK:
         if (debug > 2) {
            printk(KERN_INFO "Sending %d pulses to the haptic device\n", haptic_count_default);
         }
         break;
       default:
         printk(KERN_INFO "WARNING: unknown haptic driver state; no haptic pulse is being sent.\n");
         break;
      }
   }
   else if ( !strncmp(command, "signature", 9) ) {
      // Requested to provide a series of haptic feedback pulse(s)
      // The pulses are specified as a series of integers between 0 and 100,
      // representing the duty cycle of each pulse, separated by a "|"
      // character.
      // Example: signature 20|80|80|60|10
      // NOTE: the duty cycle is only relevant to PWM-based systems.  On GPIO
      // systems, a sequence of identical pulses will be sent.

      sscanf(command, "signature %s", signature_str);

      // Save count and duty cycle
      saved_count = haptic_count_default;
      haptic_count_default = 1;  // The signature sends one pulse at a time
      saved_pwm_duty_cycle = haptic_pwm_duty_cycle;

      token = signature_str;
      while (*token != (char)NULL) {
         haptic_pwm_duty_cycle = 0;
         while ((*token != (char)SIGNATURE_DELIMITER) && (*token != (char)NULL)) {
            if ((*token >= '0') && (*token <= '9')) {
               haptic_pwm_duty_cycle *= 10;
               haptic_pwm_duty_cycle += *token - '0';
               token++;
            }
            else {
               printk("ERROR: Invalid character in haptic signature string: %1s\n", token);
               printk("       signature_str = %s\n", signature_str);
               token++;
               break;
            }
         }
         if (*token == (char)SIGNATURE_DELIMITER) {
            token++;
         }
         if (debug > 2) {
            printk("token = %s, duty_cyle = %d\n", token, haptic_pwm_duty_cycle);
         }
         result = do_thump();

         switch (result) {
         case HAPTIC_DEV_OFF:
            printk(KERN_INFO "WARNING: the driver is suspended; no haptic pulse is being sent.\n");
            break;
         case HAPTIC_ZERO_COUNT:
            printk(KERN_INFO "WARNING: haptic_count_default is zero or negative; no haptic pulse is being sent.\n");
            break;
         case HAPTIC_BUSY:
            printk(KERN_INFO "WARNING: haptic device is busy; no additional haptic pulse is being sent.\n");
            break;
         case HAPTIC_OK:
            if (debug > 2) {
               printk(KERN_INFO "Sending %d pulses to the haptic device\n", haptic_count_default);
            }
            break;
          default:
            printk(KERN_INFO "WARNING: unknown haptic driver state; no haptic pulse is being sent.\n");
            break;
         }
      }  
      // Restore saved haptic parameters
      haptic_count_default = saved_count;
      haptic_pwm_duty_cycle = saved_pwm_duty_cycle;
   }
   else if ( !strncmp(command, "wr_pwmcr", 8) ) {
      // Requested to write PWMCR register
      sscanf(command, "wr_pwmcr %d", &keycode);
      printk(KERN_INFO "Writing 0x%X to PWMCR\n", keycode);
      WR_MXC_REG(PWMCR, keycode);
   }
   else if ( !strncmp(command, "wr_pwmsr", 8) ) {
      // Requested to write PWMSR register
      sscanf(command, "wr_pwmsr %d", &keycode);
      printk(KERN_INFO "Writing 0x%X to PWMSR\n", keycode);
      WR_MXC_REG(PWMSR, keycode);
   }
   else if ( !strncmp(command, "wr_pwmir", 8) ) {
      // Requested to write PWMIR register
      sscanf(command, "wr_pwmir %d", &keycode);
      printk(KERN_INFO "Writing 0x%X to PWMIR\n", keycode);
      WR_MXC_REG(PWMIR, keycode);
   }
   else if ( !strncmp(command, "wr_pwmsar", 9) ) {
      // Requested to write PWMSAR register
      sscanf(command, "wr_pwmsar %d", &keycode);
      printk(KERN_INFO "Writing 0x%X to PWMSAR\n", keycode);
      WR_MXC_REG(PWMSAR, keycode);
   }
   else if ( !strncmp(command, "wr_pwmpr", 8) ) {
      // Requested to write PWMPR register
      sscanf(command, "wr_pwmpr %d", &keycode);
      printk(KERN_INFO "Writing 0x%X to PWMPR\n", keycode);
      WR_MXC_REG(PWMPR, keycode);
   }
   else {
      printk(KERN_ERR "ERROR: Unrecognized haptic command\n");
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
static int __devinit haptic_probe(struct platform_device *pdev)
{
   int rqstatus = 0;
   int error;
   t_regulator_voltage voltage;

   printk("<1>[haptic_probe] Starting...\n");

   /* Create proc entry */
   proc_entry = create_proc_entry(HAPTIC_PROC_FILE, 0644, NULL);
   if (proc_entry == NULL) {
      printk(KERN_INFO "haptic: Couldn't create proc entry\n");
      return -ENOMEM;
   } else {
      proc_entry->read_proc = haptic_proc_read;
      proc_entry->write_proc = haptic_proc_write;
      proc_entry->owner = THIS_MODULE;
   }


   // Initialize the array that contains the information for the
   // haptic GPIO line of the various hardware platforms
   line_info[MARIO] = MARIO_HAPTIC_GPIO;
   line_info[TURING_V0] = TURING_V0_HAPTIC_GPIO;
   line_info[TURING_EVT1] = TURING_EVT1_HAPTIC_GPIO;
   line_info[NELL] = NELL_HAPTIC_GPIO;

   // Set dinamically the line informarion depending on the version passed
   // as a parameter
   if ((scroll_rev >= 0) && (scroll_rev < MAX_VERSIONS)) {
      haptic_gpio = line_info[scroll_rev];
   }
   else {
      printk("<1>[haptic_init] *** ERROR: scroll_rev=%d is invalid; must be 0-%d\n", scroll_rev, (MAX_VERSIONS-1));
      haptic_gpio = (iomux_pin_name_t) NULL;

      return -1;
   }

   // Initialize the haptic device, depending on the hardware version
   if (haptic_rev == 0) {
      // GPIO version
      // Initialize the haptic device timer structure
      init_timer(&haptic_timer);  // Add timer to kernel list of timers
      haptic_timer.data = 0;  // Haptic device starts inactive
      haptic_timer.function = haptic_timer_timeout;

      // Set the haptic device's GPIO line as output and set to 1 ("off" - inverse logic)
      if (haptic_gpio != (iomux_pin_name_t)NULL) {
         if (mxc_request_iomux(haptic_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO)) {
            printk("<1>[haptic_probe] *** ERROR: could not obtain GPIO pin for HAPTIC line\n");
         }
         else {
            mxc_set_gpio_direction(haptic_gpio, 0);
            mxc_set_gpio_dataout(haptic_gpio, HAPTIC_OFF);
            // Initialize the voltage supply registers for the haptic device
            pmic_power_vib_pin_en(false);  // Disable VIBEN pin control
            if ((vvib_voltage < VVIB_1_3V) || (vvib_voltage > VVIB_3V)) {
               printk("<1>[haptic_probe] *** WARNING: vvib_voltage was out of range; resetting to default value\n");
               vvib_voltage = VVIB_VOLTAGE_DEFAULT;
            }
            voltage.vvib = vvib_voltage;
            pmic_power_regulator_set_voltage(REGU_VVIB, voltage);
            // Turn on the voltage supply to the haptic device
            pmic_power_regulator_on(REGU_VVIB);
            haptic_state = HAPTIC_AVAILABLE;
         }
      }
      else {
         printk("<1>[haptic_probe] *** NOTE: HAPTIC GPIO defined as NULL\n");
      }
   }
   else {
      // PWM version
      // Get the PWMO pin configured for PWM functionality
      mxc_request_iomux(MX31_PIN_PWMO, OUTPUTCONFIG_FUNC, INPUTCONFIG_FUNC);
      DISABLE_PWM;  // Make sure PWM is disabled for now
      DISABLE_PWM_CLK;  // Turn off PWM clock
      rqstatus = request_irq(INT_PWM, (irq_handler_t) pwm_irq_handler, 0, "haptic", NULL);
      if (rqstatus != 0) {
         printk("<1>[haptic_probe] *** ERROR: INT_PWM = 0x%X; request status = %d\n", INT_PWM, rqstatus);
      }

      // Initialize the voltage supply registers for the haptic device
      pmic_power_vib_pin_en(false);  // Disable VIBEN pin control
      if ((vvib_voltage < VVIB_1_3V) || (vvib_voltage > VVIB_3V)) {
         printk("<1>[haptic_probe] *** WARNING: vvib_voltage was out of range; resetting to default value\n");
         vvib_voltage = VVIB_VOLTAGE_DEFAULT;
      }
      voltage.vvib = vvib_voltage;
      pmic_power_regulator_set_voltage(REGU_VVIB, voltage);
      // Turn on the voltage supply to the haptic device
      pmic_power_regulator_on(REGU_VVIB);
      haptic_state = HAPTIC_AVAILABLE;
   }

   printk("<1>[haptic_probe] GPIOs and IRQs have been set up\n");

   // Register the device file

   error = misc_register(&haptic_dev);
   if (error) {
      printk(KERN_ERR "haptic_dev: failed to register device\n");
      return error;
   }
   
   return 0;
}


/*!
 * Dissociates the driver from the haptic device.
 *
 * @param   pdev  the device structure used to give information on which SDHC
 *                to remove
 *
 * @return  The function always returns 0.
 */
static int __devexit haptic_remove(struct platform_device *pdev)
{
   if (haptic_rev == 0) {
      if (haptic_gpio != (iomux_pin_name_t)NULL) {
         // Haptic line is output and has no IRQ
         mxc_free_iomux(haptic_gpio, OUTPUTCONFIG_GPIO, INPUTCONFIG_GPIO);
      }
      if (haptic_timer.data != 0) {
         del_timer(&haptic_timer);
         haptic_timer.data = 0;
      }
      // Turn off power supply to haptic device
      pmic_power_regulator_off(REGU_VVIB);
   }
   else {
      DISABLE_PWM; // Turn off PWM output
      DISABLE_PWM_CLK;  // Turn off PWM clock
      free_irq(INT_PWM, NULL); // Release PWM IRQ
      // Turn off power supply to haptic device
      pmic_power_regulator_off(REGU_VVIB);
      haptic_state = HAPTIC_DEV_OFF;
   }

   remove_proc_entry(HAPTIC_PROC_FILE, &proc_root);
   misc_deregister(&haptic_dev);

   printk("<1>[haptic_remove] IRQs released and supplies stopped.\n");

   return 0;
}


/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver haptic_driver = {
        .driver = {
                   .name = "haptic",
                   .owner  = THIS_MODULE,
                   },
        .suspend = haptic_suspend,
        .resume = haptic_resume,
        .probe = haptic_probe,
        .remove = __devexit_p(haptic_remove),
};

/*!
 * This function is called for module initialization.
 * It registers the haptic driver.
 *
 * @return      0 on success and a non-zero value on failure.
 */
static int __init haptic_init(void)
{
        platform_driver_register(&haptic_driver);
        haptic_probe(NULL);
        printk(KERN_INFO "Haptic driver loaded\n");
        return 0;
}

/*!
 * This function is called whenever the module is removed from the kernel. It
 * unregisters the haptic driver from kernel.
 */
static void __exit haptic_exit(void)
{
        haptic_remove(NULL);
        platform_driver_unregister(&haptic_driver);
}



module_init(haptic_init);
module_exit(haptic_exit);

// Enable other drivers to activate haptic device
EXPORT_SYMBOL(do_thump);

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("Haptic Device Driver");
MODULE_LICENSE("GPL");
