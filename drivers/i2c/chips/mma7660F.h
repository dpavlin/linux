//
// Definitions for the Freescale MMA7660F Accelerometer driver
//

#include <linux/input.h>

// Modify these 2 defines when adding other hardware revisions!
#define MAX_VERSIONS 2
#define MAX_LINES 2

// GPIO line definitions
// NOTE: i2c lines are not defined here because we use the existing i2c
//       driver.  We only need to define the interrupt lines specific to
//       the Freescale Accelerometer

// For Mario:
#define MARIO_INT_GPIO    MX31_PIN_UART2_TXD

// For Nell:
#define NELL_INT_GPIO     MX31_PIN_CSI_D15

// IRQ definitions, only for those inputs that generate interrupts
#define INT_IRQ           IOMUX_TO_IRQ(int_gpio)

// Strings sent by driver to uevent
#define ORIENTATION_STRING_UP           "orientation=up"
#define ORIENTATION_STRING_DOWN         "orientation=down"
#define ORIENTATION_STRING_LEFT         "orientation=left"
#define ORIENTATION_STRING_RIGHT        "orientation=right"
#define ORIENTATION_STRING_FACE_UP      "orientation=face_up"
#define ORIENTATION_STRING_FACE_DOWN    "orientation=face_down"
#define ORIENTATION_STRING_SHAKE        "orientation=shake"
#define ORIENTATION_STRING_TAP          "orientation=tap"
#define ORIENTATION_STRING_DOUBLE_WAVE  "orientation=double_wave"

// Strings sent by driver to proc file read
#define PROC_ORIENTATION_STRING_UP           "orientationUp"
#define PROC_ORIENTATION_STRING_DOWN         "orientationDown"
#define PROC_ORIENTATION_STRING_LEFT         "orientationLeft"
#define PROC_ORIENTATION_STRING_RIGHT        "orientationRight"
#define PROC_ORIENTATION_STRING_FACE_UP      "orientationFaceUp"
#define PROC_ORIENTATION_STRING_FACE_DOWN    "orientationFaceDown"
#define PROC_ORIENTATION_STRING_UNKNOWN      "orientationUnknown"
#define PROC_ORIENTATION_STRING_INVALID      "orientationInvalid"
#define PROC_ORIENTATION_STRING_SHAKE        "orientationShake"
#define PROC_ORIENTATION_STRING_TAP          "orientationTap"
#define PROC_ORIENTATION_STRING_DOUBLE_WAVE  "orientationDoubleWave"

#define OUTPUTCONFIG_GPIO 0
#define INPUTCONFIG_GPIO  1

// The following flag is not defined in mxc_i2c.h but we need it
#define MXC_I2C_FLAG_WRITE 0

// Values specific to the Freescale MMA7660F Accelerometer
// Note: bit constants included below the register where they apply
#define ACCEL_BUS_ID       0
#define ACCEL_I2C_ADDRESS  0x4C
#define ACCEL_REG_ADDR_LEN 1
#define ACCEL_REG_DATA_LEN 1
#define ACCEL_XOUT_REG     0x00
	#define XOUT_DATA_MASK       0x3F
	#define XOUT_ALERT           0x40
#define ACCEL_YOUT_REG     0x01
	#define YOUT_DATA_MASK       0x3F
	#define YOUT_ALERT           0x40
#define ACCEL_ZOUT_REG     0x02
	#define ZOUT_DATA_MASK       0x3F
	#define ZOUT_ALERT           0x40
#define ACCEL_TILT_REG     0x03
	#define TILT_BAFR            0x03
	#define TILT_BAFR_FRONT      0x01
	#define TILT_BAFR_BACK       0x02
	#define TILT_POLA            0x1C
	#define TILT_POLA_RIGHT      0x01 << 2
	#define TILT_POLA_LEFT       0x02 << 2
	#define TILT_POLA_DOWN       0x05 << 2
	#define TILT_POLA_UP         0x06 << 2
	#define TILT_PULSE           0x20
	#define TILT_ALERT           0x40
	#define TILT_SHAKE           0x80
#define ACCEL_SRST_REG     0x04
	#define SRST_AMSRS           0x01
	#define SRST_AWSRS           0x01
#define ACCEL_SPCNT_REG    0x05
#define ACCEL_INTSU_REG    0x06
	#define INTSU_FBINT          0x01
	#define INTSU_PLINT          0x02
	#define INTSU_PDINT          0x04
	#define INTSU_ASINT          0x08
	#define INTSU_GINT           0x10
	#define INTSU_SHINTZ         0x20
	#define INTSU_SHINTY         0x40
	#define INTSU_SHINTX         0x80
#define ACCEL_MODE_REG     0x07
	#define MODE_ACTIVE          0x01
	#define MODE_TON             0x04
	#define MODE_AWE             0x08
	#define MODE_ASE             0x01
	#define MODE_SCPS            0x02
	#define MODE_IPP             0x04
	#define MODE_IAH             0x08
#define ACCEL_SR_REG       0x08
	#define SR_AMSR              0x07
	#define SR_AMSR_AMPD         0x00
	#define SR_AMSR_AM64         0x01
	#define SR_AMSR_AM32         0x02
	#define SR_AMSR_AM16         0x03
	#define SR_AMSR_AM8          0x04
	#define SR_AMSR_AM4          0x05
	#define SR_AMSR_AM2          0x06
	#define SR_AMSR_AM1          0x07
	#define SR_AWSR              0x18
	#define SR_AWSR_AW32         0x00 << 3
	#define SR_AWSR_AW16         0x01 << 3
	#define SR_AWSR_AW8          0x02 << 3
	#define SR_AWSR_AW1          0x03 << 3
	#define SR_FILT              0xE0
	#define SR_FILT_DISABLED     0x00 << 5
	#define SR_FILT_2            0x01 << 5
	#define SR_FILT_3            0x02 << 5
	#define SR_FILT_4            0x03 << 5
	#define SR_FILT_5            0x04 << 5
	#define SR_FILT_6            0x05 << 5
	#define SR_FILT_7            0x06 << 5
	#define SR_FILT_8            0x07 << 5
#define ACCEL_PDET_REG     0x09
	#define SR_PDET_PDTH         0x1F
	#define SR_PDET_XDA          0x20
	#define SR_PDET_YDA          0x40
	#define SR_PDET_ZDA          0x80
#define ACCEL_PD_REG       0x0A


// Definitions for the array of input line definitions
//
// Hardware version
enum {
   MARIO,
   NELL
};

// IOCTL commands
enum {
   WRITE_I2C_REG,
   READ_I2C_REG
};

// States for device orientation state-machine
enum {
   IRQ_UNKNOWN,
   IRQ_READ_ORIENTATION,
   IRQ_IGNORE,
   IRQ_WAIT_FOR_READING
}; 

// States for device polling state-machine
enum {
   POLL_INIT,
   POLL_SLEEP,
   POLL_WAITING_FOR_IRQ,
   POLL_TIME_TO_READ_I2C
}; 

// Device orientation states
enum {
   DEVICE_INIT_ORIENTATION,
   DEVICE_UNKNOWN,
   DEVICE_UP,
   DEVICE_DOWN,
   DEVICE_LEFT,
   DEVICE_RIGHT,
   DEVICE_FACE_UP,
   DEVICE_FACE_DOWN,
}; 

// External function definitions
static int __init mma7660F_init(void);
static void __exit mma7660F_exit(void);
//static void mma7660F_poll(struct input_polled_dev *dev);
static irqreturn_t mma7660F_irq(int irq, void *data, struct pt_regs *r);

//extern int gpio_set_irq_type(u32 irq, u32 type);
extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
extern void mario_enable_pullup_pad(iomux_pin_name_t pin);
extern void mario_disable_pullup_pad(iomux_pin_name_t pin);

