//
// Definitions for trackball driver
//

// Modify these 2 defines when adding other hardware revisions!
#define MAX_VERSIONS 2
#define MAX_LINES 5

// GPIO line definitions
// NOTE: If any line is wired into the keypad array, not into GPIOs,
//       define it here as (iomux_pin_name_t)NULL.  In addition, the
//       appropriate code must be programmed in the keyboard driver.

// For Mario:
#define MARIO_TRBL_UP_GPIO     MX31_PIN_STX0
#define MARIO_TRBL_DOWN_GPIO   MX31_PIN_SIMPD0
#define MARIO_TRBL_LEFT_GPIO   MX31_PIN_SVEN0
#define MARIO_TRBL_RIGHT_GPIO  MX31_PIN_SRST0
#define MARIO_TRBL_SELECT_GPIO MX31_PIN_SCLK0

// For the first Turing prototype build:
#define TURING_V0_TRBL_UP_GPIO     MX31_PIN_SRST0
#define TURING_V0_TRBL_DOWN_GPIO   MX31_PIN_SRX0
#define TURING_V0_TRBL_LEFT_GPIO   MX31_PIN_SIMPD0
#define TURING_V0_TRBL_RIGHT_GPIO  MX31_PIN_SCLK0
#define TURING_V0_TRBL_SELECT_GPIO (iomux_pin_name_t) NULL

// IRQ definitions, only for those inputs that generate interrupts
#define TRBL_UP_IRQ      IOMUX_TO_IRQ(trbl_up_gpio)
#define TRBL_DOWN_IRQ    IOMUX_TO_IRQ(trbl_down_gpio)
#define TRBL_LEFT_IRQ    IOMUX_TO_IRQ(trbl_left_gpio)
#define TRBL_RIGHT_IRQ   IOMUX_TO_IRQ(trbl_right_gpio)
#define TRBL_SELECT_IRQ  IOMUX_TO_IRQ(trbl_select_gpio)

#define OUTPUTCONFIG_GPIO 0
#define INPUTCONFIG_GPIO  1

// Definitions for the array of input line definitions
//
// Hardware version
enum {
   MARIO,
   TURING_V0
};
// Line type
enum {
   TRBL_UP,
   TRBL_DOWN,
   TRBL_LEFT,
   TRBL_RIGHT,
   TRBL_SELECT
};

// External function definitions
static int __init trackball_init(void);
static void __exit trackball_exit(void);
static void trackball_timer_timeout(unsigned long timer_data);
static void hold_timer_timeout(unsigned long timer_data);
static void release_timer_timeout(unsigned long timer_data);
static irqreturn_t trackball_irq_UP(int irq, void *data, struct pt_regs *r);
static irqreturn_t trackball_irq_DOWN(int irq, void *data, struct pt_regs *r);
static irqreturn_t trackball_irq_LEFT(int irq, void *data, struct pt_regs *r);
static irqreturn_t trackball_irq_RIGHT(int irq, void *data, struct pt_regs *r);
static irqreturn_t trackball_irq_SELECT(int irq, void *data, struct pt_regs *r);

//extern int gpio_set_irq_type(u32 irq, u32 type);
extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
