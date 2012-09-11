//
// Definitions for the Freescale MMA7455L Accelerometer driver
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
// NOTE: use only one define for each INT line!
#define MARIO_INT1_DRDY_GPIO    MX31_PIN_UART2_TXD
#define MARIO_INT2_GPIO         MX31_PIN_UART2_RXD

// For Nell:
// NOTE: use only one define for each INT line!
#define NELL_INT1_DRDY_GPIO    MX31_PIN_CSI_D15
#define NELL_INT2_GPIO         MX31_PIN_CSI_D14

// IRQ definitions, only for those inputs that generate interrupts
#define INT1_DRDY_IRQ     IOMUX_TO_IRQ(int1_drdy_gpio)
#define INT2_IRQ          IOMUX_TO_IRQ(int2_gpio)

// Strings sent by driver to uevent
#define ORIENTATION_STRING_UP         "orientation=up"
#define ORIENTATION_STRING_DOWN       "orientation=down"
#define ORIENTATION_STRING_LEFT       "orientation=left"
#define ORIENTATION_STRING_RIGHT      "orientation=right"
#define ORIENTATION_STRING_FACE_UP    "orientation=face_up"
#define ORIENTATION_STRING_FACE_DOWN  "orientation=face_down"
#define ORIENTATION_STRING_FREE_FALL  "orientation=free_fall"

// Strings sent by driver to proc file read
#define PROC_ORIENTATION_STRING_UP         "orientationUp"
#define PROC_ORIENTATION_STRING_DOWN       "orientationDown"
#define PROC_ORIENTATION_STRING_LEFT       "orientationLeft"
#define PROC_ORIENTATION_STRING_RIGHT      "orientationRight"
#define PROC_ORIENTATION_STRING_FACE_UP    "orientationFaceUp"
#define PROC_ORIENTATION_STRING_FACE_DOWN  "orientationFaceDown"
#define PROC_ORIENTATION_STRING_FREE_FALL  "orientationFreeFall"
#define PROC_ORIENTATION_STRING_UNKNOWN    "orientationUnknown"
#define PROC_ORIENTATION_STRING_INVALID    "orientationInvalid"

#define OUTPUTCONFIG_GPIO 0
#define INPUTCONFIG_GPIO  1

// The following flag is not defined in mxc_i2c.h but we need it
#define MXC_I2C_FLAG_WRITE 0

// Values specific to the Freescale MMA7455L Accelerometer
// Note: bit constants included below the register where they apply
#define ACCEL_BUS_ID       0
#define ACCEL_I2C_ADDRESS  0x1D
#define ACCEL_REG_ADDR_LEN 1
#define ACCEL_REG_DATA_LEN 1
#define ACCEL_XOUTL_REG    0x00
#define ACCEL_XOUTH_REG    0x01
#define ACCEL_YOUTL_REG    0x02
#define ACCEL_YOUTH_REG    0x03
#define ACCEL_ZOUTL_REG    0x04
#define ACCEL_ZOUTH_REG    0x05
#define ACCEL_XOUT8_REG    0x06
#define ACCEL_YOUT8_REG    0x07
#define ACCEL_ZOUT8_REG    0x08
#define ACCEL_STATUS_REG   0x09
	#define STATUS_DRDY           0x01
	#define STATUS_DOVR           0x02
	#define STATUS_PERR           0x04
#define ACCEL_DETSRC_REG   0x0A
#define ACCEL_TOUT_REG     0x0B
#define ACCEL_I2CAD_REG    0x0D
	#define I2CAD_I2CDIS          0x80
#define ACCEL_USRINF_REG   0x0E
#define ACCEL_WHOAMI_REG   0x0F
#define ACCEL_XOFFL_REG    0x10
#define ACCEL_XOFFH_REG    0x11
#define ACCEL_YOFFL_REG    0x12
#define ACCEL_YOFFH_REG    0x13
#define ACCEL_ZOFFL_REG    0x14
#define ACCEL_ZOFFH_REG    0x15
#define ACCEL_MCTL_REG     0x16
	#define MCTL_MODE0            0x01
	#define MCTL_MODE1            0x02
	#define MCTL_GLVL0            0x04
	#define MCTL_GLVL1            0x08
	#define MCTL_STON             0x10
	#define MCTL_SPI3             0x20
	#define MCTL_DRPD             0x40
	#define STANDBY_MODE          0x00
	#define MEASUREMENT_MODE      MCTL_MODE0
	#define LEVEL_DETECTION_MODE  MCTL_MODE1
	#define PULSE_DETECTION_MODE  MCTL_MODE1 | MCTL_MODE0
	#define MEASUREMENT_RANGE_2G  MCTL_GLVL0
	#define MEASUREMENT_RANGE_4G  MCTL_GLVL1
	#define MEASUREMENT_RANGE_8G  0x00
#define ACCEL_INTRST_REG   0x17
	#define INTRST_CLR_INT1       0x01
	#define INTRST_CLR_INT2       0x02
#define ACCEL_CTL1_REG     0x18
	#define CTL1_INTPIN           0x01
	#define CTL1_INTREG_P_L       0x02
	#define CTL1_INTREG_SP_       0x04
	#define CTL1_XDA              0x08
	#define CTL1_YDA              0x10
	#define CTL1_ZDA              0x20
	#define CTL1_THOPT            0x40
	#define CTL1_DFBW             0x80
#define ACCEL_CTL2_REG     0x19
	#define CTL2_LDPL             0x01
	#define CTL2_PDPL             0x02
	#define CTL2_DRVO             0x04
#define ACCEL_LDTH_REG     0x1A
#define ACCEL_PDTH_REG     0x1B
#define ACCEL_PW_REG       0x1C
#define ACCEL_LT_REG       0x1D
#define ACCEL_TW_REG       0x1E

// Definitions for the array of input line definitions
//
// Hardware version
enum {
   MARIO,
   NELL
};
// Line type
enum {
   INT1_DRDY,
   INT2
};

// IOCTL commands
enum {
   WRITE_I2C_REG,
   READ_I2C_REG
};

// States for device orientation polling state-machine
enum {
   ORIENTATION_INIT,
   ORIENTATION_IDLE,
   ORIENTATION_COUNTING
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
   DEVICE_FREE_FALL
}; 

// External function definitions
static int __init mma7455L_init(void);
static void __exit mma7455L_exit(void);
static irqreturn_t mma7455L_irq(int irq, void *data, struct pt_regs *r);

//extern int gpio_set_irq_type(u32 irq, u32 type);
extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
