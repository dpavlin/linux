//
// Definitions for Alps HGDEST011A magnetic sensor
//

#include <linux/input.h>

// Modify this define when adding other hardware revisions!
#define MAX_VERSIONS 2

// GPIO line definitions
// NOTE: If any line is wired into the keypad array, not into GPIOs,
//       define it here as (iomux_pin_name_t)NULL.  In addition, the
//       appropriate code must be programmed in the keyboard driver.

// For Turing EVT1 and Nell Prototype
#define TURING_EVT1_MAG_SENSOR_GPIO    MX31_PIN_ATA_RESET_B

// For Turing EVT1.5 (and later), and Nell
#define TURING_NELL_MAG_SENSOR_GPIO    MX31_PIN_CAPTURE

// IRQ definition for the one input that generates interrupts
#define MAG_SENSOR_IRQ      IOMUX_TO_IRQ(mag_sensor_gpio)

#define OUTPUTCONFIG_GPIO 0
#define INPUTCONFIG_GPIO  1

// Magnetic sensor input states
#define MAG_SENSOR_OFF    1
#define MAG_SENSOR_ON     0
#define MAG_SENSOR_START  -1

// Strings sent by driver to uevent
#define MAG_STATE_OPEN   "state=open"
#define MAG_STATE_CLOSED "state=closed"

// Definition for the input line definition
//
// Hardware version
enum {
   TURING_EVT1,
   TURING_NELL
};

// External function definitions
static int __init mag_sensor_init(void);
static void __exit mag_sensor_exit(void);
static void debounce_timer_timeout(unsigned long timer_data);
static irqreturn_t mag_sensor_irq(int irq, void *data, struct pt_regs *r);

//extern int gpio_set_irq_type(u32 irq, u32 type);
extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
