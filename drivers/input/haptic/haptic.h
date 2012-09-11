//
// Definitions for haptic device driver
//

#include <linux/input.h>

// Modify these 2 defines when adding other hardware revisions!
#define MAX_VERSIONS 4

// GPIO line definitions
// NOTE: If any line is wired into the keypad array, not into GPIOs,
//       define it here as (iomux_pin_name_t)NULL.  In addition, the
//       appropriate code must be programmed in the keyboard driver.

// For Mario:
#define MARIO_HAPTIC_GPIO MX31_PIN_DSR_DCE1

// For the first Turing prototype build:
#define TURING_V0_HAPTIC_GPIO (iomux_pin_name_t) NULL

// For Turing EVT1
#define TURING_EVT1_HAPTIC_GPIO MX31_PIN_DSR_DCE1

// For Nell
#define NELL_HAPTIC_GPIO MX31_PIN_USB_PWR

#define OUTPUTCONFIG_GPIO 0
#define OUTPUTCONFIG_FUNC 1
#define INPUTCONFIG_GPIO  1
#define INPUTCONFIG_FUNC  2

// Haptic device control
#define HAPTIC_OFF 0
#define HAPTIC_ON  1

// PWM registers used for the haptic control pin in hardware platforms
// after Turing EVT1.5
#define CCM_BASE       IO_ADDRESS(CCM_BASE_ADDR)
#define CCM_CGR1       (CCM_BASE + 0x24)
#define CCM_CGR1_PWM_CLK_MASK     0x00003000
#define CCM_CGR1_DISABLE_PWM_CLK  0x00000000
#define CCM_CGR1_ENABLE_PWM_CLK   0x00000003 << 12

#define PWM_REG_BASE   IO_ADDRESS(PWM_BASE_ADDR)
#define PWMCR          (PWM_REG_BASE + 0x0000)
#define PWMSR          (PWM_REG_BASE + 0x0004)
#define PWMIR          (PWM_REG_BASE + 0x0008)
#define PWMSAR         (PWM_REG_BASE + 0x000C)
#define PWMPR          (PWM_REG_BASE + 0x0010)
#define PWMCNR         (PWM_REG_BASE + 0x0014)

// Relevant PWM constants
#define PWM_SOURCE_FREQ 66500000
#define PWM_COUNT_RANGE 65536

#define PWMCR_EN_MASK              0x00000001
#   define PWMCR_DISABLE_PWM       0x00000000
#   define PWMCR_ENABLE_PWM        0x00000001
#define PWMCR_REPEAT_MASK          0x00000006
#   define PWMCR_REPEAT_1          0x00000000 << 1
#   define PWMCR_REPEAT_2          0x00000001 << 1
#   define PWMCR_REPEAT_4          0x00000002 << 1
#   define PWMCR_REPEAT_8          0x00000003 << 1
#define PWMCR_SWR_MASK             0x00000008
#   define PWMCR_SWR_OFF           0x00000000 << 3
#   define PWMCR_SWR_ON            0x00000001 << 3
#define PWMCR_PRESCALER_MASK       0x0000FFF0
#   define PWMCR_PRESCALER_SHIFT   4
#define PWMCR_CLKSRC_MASK          0x00030000
#   define PWMCR_CLK_OFF           0x00000000 << 16
#   define PWMCR_IPG_CLK           0x00000001 << 16
#   define PWMCR_IPG_CLK_HIGHFREQ  0x00000002 << 16
#   define PWMCR_IPG_CLK_32K       0x00000003 << 16
#define PWMCR_POUTC_MASK           0x000C0000
#   define PWMCR_SET_THEN_CLR      0x00000000 << 18
#   define PWMCR_CLR_THEN_SET      0x00000001 << 18
#   define PWMCR_DISCONN_OUTPUT    0x00000002 << 18
#define PWMCR_HCTR_MASK            0x00100000
#   define PWMCR_HCTR_OFF          0x00000000 << 20
#   define PWMCR_HCTR_ON           0x00000001 << 20
#define PWMCR_BCTR_MASK            0x00200000
#   define PWMCR_BCTR_OFF          0x00000000 << 21
#   define PWMCR_BCTR_ON           0x00000001 << 21
#define PWMCR_DBGEN_MASK           0x00400000
#   define PWMCR_DBGEN_OFF         0x00000000 << 22
#   define PWMCR_DBGEN_ON          0x00000001 << 22
#define PWMCR_WAITEN_MASK          0x00800000
#   define PWMCR_WAITEN_OFF        0x00000000 << 23
#   define PWMCR_WAITEN_ON         0x00000001 << 23
#define PWMCR_DOZEN_MASK           0x01000000
#   define PWMCR_DOZEN_OFF         0x00000000 << 24
#   define PWMCR_DOZEN_ON          0x00000001 << 24
#define PWMCR_STOPEN_MASK          0x02000000
#   define PWMCR_STOPEN_OFF        0x00000000 << 25
#   define PWMCR_STOPEN_ON         0x00000001 << 25
#define PWMCR_FWM                  0x0C000000
#   define PWMCR_FWM_1             0x00000000 << 26
#   define PWMCR_FWM_2             0x00000001 << 26
#   define PWMCR_FWM_3             0x00000002 << 26
#   define PWMCR_FWM_4             0x00000003 << 26

#define PWMSR_FIFOAV_MASK          0x00000007
#define PWMSR_FIFOAV_FULL          0x00000004
#define PWMSR_FE_MASK              0x00000008
#   define PWMSR_FE_CLR            0x00000001 << 3
#define PWMSR_ROV_MASK             0x00000010
#   define PWMSR_ROV_CLR           0x00000001 << 4
#define PWMSR_CMP_MASK             0x00000020
#   define PWMSR_CMP_CLR           0x00000001 << 5
#define PWMSR_FWE_MASK             0x00000040
#   define PWMSR_FWE_CLR           0x00000001 << 6

#define PWMIR_FIE_MASK             0x00000001
#   define PWMIR_FIE_DISABLE       0x00000000
#   define PWMIR_FIE_ENABLE        0x00000001
#define PWMIR_RIE_MASK             0x00000002
#   define PWMIR_RIE_DISABLE       0x00000000 << 1
#   define PWMIR_RIE_ENABLE        0x00000001 << 1
#define PWMIR_CIE_MASK             0x00000004
#   define PWMIR_CIE_DISABLE       0x00000000 << 2
#   define PWMIR_CIE_ENABLE        0x00000001 << 2

#define HAPTIC_WAIT_TIMEOUT  10000000


// Delimiter for "signature" command
#define SIGNATURE_DELIMITER '|'

// Haptic states
enum {
   HAPTIC_DEV_OFF,
   HAPTIC_ZERO_COUNT,
   HAPTIC_BUSY,
   HAPTIC_AVAILABLE,
   HAPTIC_OK,
   HAPTIC_TIMEOUT
};

// Definitions for the array of input line definitions
//
// Hardware version
enum {
   MARIO,
   TURING_V0,
   TURING_EVT1,
   NELL
};

// External function definitions
static int __init haptic_init(void);
static void __exit haptic_exit(void);
static int do_thump(void);
static void haptic_timer_timeout(unsigned long timer_data);
static irqreturn_t pwm_irq_handler(int irq, void *data, struct pt_regs *r);

extern int mxc_request_iomux(int pin, int out, int in);
extern void mxc_free_iomux(int pin, int out, int in);
