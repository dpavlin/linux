//
// Definitions for 5-Way Switch 5-way driver
//

#include <linux/input.h>

// Modify these 2 defines when adding other hardware revisions!
#define MAX_VERSIONS 3
#define MAX_LINES 5

// GPIO line definitions
// NOTE: If any line is wired into the keypad array, not into GPIOs,
//       define it here as (iomux_pin_name_t)NULL.  In addition, the
//       appropriate code must be programmed in the keyboard driver.

// For Mario:
#define MARIO_FIVEWAY_UP_GPIO     MX31_PIN_DCD_DTE1
#define MARIO_FIVEWAY_DOWN_GPIO   MX31_PIN_DCD_DCE1
#define MARIO_FIVEWAY_LEFT_GPIO   MX31_PIN_RI_DTE1
#define MARIO_FIVEWAY_RIGHT_GPIO  MX31_PIN_DTR_DCE2
#define MARIO_FIVEWAY_SELECT_GPIO MX31_PIN_DSR_DTE1

// For the first Turing prototype build:
#define TURING_V0_FIVEWAY_UP_GPIO     (iomux_pin_name_t) NULL
#define TURING_V0_FIVEWAY_DOWN_GPIO   (iomux_pin_name_t) NULL
#define TURING_V0_FIVEWAY_LEFT_GPIO   (iomux_pin_name_t) NULL
#define TURING_V0_FIVEWAY_RIGHT_GPIO  (iomux_pin_name_t) NULL
#define TURING_V0_FIVEWAY_SELECT_GPIO (iomux_pin_name_t) NULL

// For Turing EVT1 and Nell
#define NELL_TURING_V1_FIVEWAY_UP_GPIO     MX31_PIN_CSI_D13
#define NELL_TURING_V1_FIVEWAY_DOWN_GPIO   MX31_PIN_CSI_D10
#define NELL_TURING_V1_FIVEWAY_LEFT_GPIO   MX31_PIN_CSI_D12
#define NELL_TURING_V1_FIVEWAY_RIGHT_GPIO  MX31_PIN_CSI_D6
#define NELL_TURING_V1_FIVEWAY_SELECT_GPIO MX31_PIN_CSI_D11

// IRQ definitions, only for those inputs that generate interrupts
#define FIVEWAY_UP_IRQ      IOMUX_TO_IRQ(fiveway_up_gpio)
#define FIVEWAY_DOWN_IRQ    IOMUX_TO_IRQ(fiveway_down_gpio)
#define FIVEWAY_LEFT_IRQ    IOMUX_TO_IRQ(fiveway_left_gpio)
#define FIVEWAY_RIGHT_IRQ   IOMUX_TO_IRQ(fiveway_right_gpio)
#define FIVEWAY_SELECT_IRQ  IOMUX_TO_IRQ(fiveway_select_gpio)

// Keycodes
#define FIVEWAY_KEYCODE_UP       KEY_HANGUEL
#define FIVEWAY_KEYCODE_DOWN     KEY_HANJA
#define FIVEWAY_KEYCODE_LEFT     KEY_LEFT
#define FIVEWAY_KEYCODE_RIGHT    KEY_RIGHT
#define FIVEWAY_KEYCODE_SELECT   KEY_HENKAN

#define OUTPUTCONFIG_GPIO 0
#define OUTPUTCONFIG_FUNC 1
#define INPUTCONFIG_GPIO  1
#define INPUTCONFIG_FUNC  2

// Definitions for the array of input line definitions
//
// Hardware version
enum {
   MARIO,
   TURING_V0,
   NELL_TURING_V1
};
// Line type
enum {
   FIVEWAY_UP,
   FIVEWAY_DOWN,
   FIVEWAY_LEFT,
   FIVEWAY_RIGHT,
   FIVEWAY_SELECT,
};

// Line states
#define NUM_OF_LINES 5
enum {
   UP_LINE,
   DOWN_LINE,
   LEFT_LINE,
   RIGHT_LINE,
   SELECT_LINE
};
enum {
   LINE_ASSERTED,
   LINE_DEASSERTED,
   LINE_HELD,
};

// External function definitions
static int __init fiveway_init(void);
static void __exit fiveway_exit(void);
static void debounce_timer_timeout(unsigned long timer_data);
static void auto_repeat_timer_timeout(unsigned long timer_data);
static irqreturn_t fiveway_irq(int irq, void *data, struct pt_regs *r);

// NOTE: the following is taken out for Turing
//extern int do_thump(void);

extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
extern void mario_enable_pullup_pad(iomux_pin_name_t pin);
extern void mario_disable_pullup_pad(iomux_pin_name_t pin);
