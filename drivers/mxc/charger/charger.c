/*
 * Charger driver
 *
 * Copyright 2008-2009 Amazon Technologies, Inc.  All rights reserved.
 * Jerry Katzung <katzung@lab126.com>
 * Manish Lachwani <lachwani@lab126.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpufreq.h>
#include <asm/arch/pmic_external.h>
#include <asm/arch/pmic_battery.h>
#include <asm/arch/pmic_adc.h>
#include <asm/arch/pmic_light.h>

#include "charger.h"
#include "battery.h"


#define LLOG_G_LOG_MASK chrg_log_mask
#define LLOG_KERNEL_COMP "charger"

#include <llog.h>


#define DRIVER_NAME "charger"
#define DRIVER_VERSION "version=2.1"

#define CHARGE_CURRENT_MAX		1000	/* mA */
#define POWER_DISSIPATION_MAX		1000	/* mW */
#define DEFAULT_TIMER_DELAY		10	/* seconds */
#define DEFAULT_VCHRG_SETTING		3	/* 4200 mV */
#define LOW_VOLTAGE_THRESHOLD		3400	/* mV */
#define CRITICAL_VOLTAGE_THRESHOLD	3100	/* mV */

#define PMIC_IRQ			67	/* PMIC IRQ */
static DEFINE_MUTEX(charger_wq_mutex);		/* Charger driver mutex */	
int charger_misdetect_retry = 0;		/* Track the arcotg misdetects */
EXPORT_SYMBOL(charger_misdetect_retry);

int charger_probed = 0;				/* Has the charger module been probed */
EXPORT_SYMBOL(charger_probed);

static int charger_turn_off_curri = 0;		/* Keep track of CURRI */
static int curri_test = 0;			/* Did we unsubscribe CHGCURRI? */
static int charger_suspend(struct platform_device *pdev, pm_message_t state);
static int charger_resume(struct platform_device *pdev);
static int charger_probe(struct platform_device *pdev);
static int charger_remove(struct platform_device *pdev);

static void charger_device_release(struct device *dev);

static void charger_usb4v4(struct work_struct *not_used);
static void charger_arcotg_wakeup(struct work_struct *not_used);
static DECLARE_DELAYED_WORK(charger_usb4v4_work, charger_usb4v4);
static DECLARE_DELAYED_WORK(charger_arcotg_work, charger_arcotg_wakeup);
static void charger_queue_plug_event(int state);
static void set_led(int led_state);
#ifdef CONFIG_CPU_FREQ
int charger_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data);
#endif

static int host_charging = 0;

atomic_t arcotg_callback_done = ATOMIC_INIT(0);

/* ICHRG settings
 *
 * 	Value	Max mA	(Nom mA)
 *
 *	0	0	(0)		
 *	1	85	(70)
 *	2	195	(177)
 *	3	293	(266)
 *	4	390	(355)
 *	5	488	(443)
 *	6	585	(532)
 *	7	683	(621)
 *	8	780	(709)
 *	9	878	(798)
 *	10	975	(886)
 *	11	1073	(975)
 *	12	1170	(1064)
 *	13	1268	(1152)
 *	14	1755	(1596)
 *	15	-- Fully on --
 *
 *
 * ICHRG calculation:
 *
 * ICHRG = POWER(max)/(delta V)
 *
 * where (delta V) is the difference between VCHRGRAW and either
 * BATT, BATTISNS, or BP.  (Each successive point is closer to
 * the charge FET's and captures more of the total power dissipation.)
 *
 * POWER(max) will probably be somewhere between 500mW-1000mW.
 *
 * VCHRGRAW can vary between, let's say, 3.2V (BPON) and 5.9V (overvoltage
 * protection limit).
 *
 * Since BPON is 3.2V, and VCHRGRAW could be as high as 5.9V, roughly the
 * biggest delta-V we should see is 2.7V.  If POWER(max) of 500mW is
 * acceptable, then we only draw a maximum of 185mA and still stay under
 * the power dissipation limit.
 *
 * 		POWER(max) (mW)
 * delta-V
 *   (V)	500 mW		1000 mW
 *   		Max I	ICHRG	Max I	ICHRG
 * -------	-------------	-------------
 *  2.7		185mA	1	370mA	3
 *  2.6		192	1	384	3
 *  2.5		200	2	400	4
 *  2.4		208	2	416	4
 *  2.3		217	2	434	4
 *  2.2		227	2	454	4
 *  2.1		238	2	476	4
 *  2.0		250	2	500	5
 *  1.9		263	2	526	5
 *  1.8		277	2	555	5
 *  1.7		294	3	588	6
 *  1.6		312	3	625	6
 *  1.5		333	3	666	6
 *  1.4		357	3	714	7
 *  1.3		384	3	769	7
 *  1.2		416	4	833	8
 *  1.1		454	4	909	9
 *  1.0		500	5	1000	10
 *  0.9		555	5	1111	11
 *  0.8		625	6	1250	11?
 *  0.7		714	7	1428	11?
 *  0.6		833	8	1666	11?
 *  0.5		1000	10	2000	11?
 *  0.4		1250	11?	2500	11?
 *  0.3		1666	11?	3333	11?
 *  0.2		2500	11?	5000	11?
 *  0.1		5000	11?	10000	11?
 *  0.0		NA	11?	NA	11?
 */

struct charger {
	spinlock_t		lock;
	struct platform_driver	drvr;
	struct platform_device	dev;
	struct workqueue_struct	*charger_wq;
	struct work_struct	charger_work;
	int			charger_work_queued;
	unsigned long		operations;

	struct timer_list	timer;
	int			timer_delay;	/* in seconds */

	pmic_event_callback_t	chgdeti_callback;
	pmic_event_callback_t	chrgcurri_callback;
	pmic_event_callback_t	lobathi_callback;
	pmic_event_callback_t	lobatli_callback;
	
	int			lobathi_callback_subscribed;
	int			lobatli_callback_subscribed;
	int			suspended;
	int			current_current_limit;
	int			current_ichrg;
	int			current_vchrg;
	int			current_led;
	int			vbus_present;
	int			charger_present;
	int			usb_current_limit;
	int			max_current_limit;
	int			max_power_dissipation;
	int			voltage;	/* Last BP reading */
	int			vbatt;		/* Last VBATT reading */
	int			temp_ok;	/* Charging allowed */
	int			battery_full;	/* Green LED control */
	int			(*arcotg_callback)(void *arcotg_arg, int state);
	void			*arcotg_arg;
	int			arcotg_state;
#ifdef CONFIG_CPU_FREQ
	struct notifier_block	cpufreq_transition;
#endif
};

static struct charger charger_globals = {
	.drvr = {
		.driver = {
				.name = DRIVER_NAME,
				.bus  = &platform_bus_type,
			  },
		.suspend = charger_suspend,
		.resume  = charger_resume,
		.probe   = charger_probe,
		.remove  = charger_remove,
		},
	.dev  = {
		.name	= "charger",
		.id	= -1,
		.dev =	{
			.release = charger_device_release,
			},
		},
	.operations		= 0,
	.vbus_present		= false,
	.charger_present	= false,
	.suspended		= false,
	.current_current_limit	= 0,
	.max_current_limit	= CHARGE_CURRENT_MAX,
	.current_ichrg		= 0,
	.current_vchrg		= DEFAULT_VCHRG_SETTING,
	.max_power_dissipation	= POWER_DISSIPATION_MAX,
	.voltage		= 0,
	.vbatt			= 0,
	.temp_ok		= 1,
	.battery_full		= 0,
	.arcotg_callback	= NULL,
	.arcotg_arg		= NULL,
	.arcotg_state		= 0,
};

atomic_t charger_in_scaling = ATOMIC_INIT(0);
static struct charger *charger = &charger_globals;

unsigned int	chrg_log_mask;
char	*envp_low[] = { "BATTERY=low", NULL };
char	*envp_critical[] = { "BATTERY=critical", NULL };
char	*charge_led_string[] = { "off", "yellow", "green" };
int	vchrg_table[] = { 4050, 4375, 4150, 4200, 4250, 4300, 3800, 4500 };

#define VCHRG_4200	3	// Index of 4200 mV VCHRG value
#define VCHRG_4250	4	// Index of 4250 mV VCHRG value

int	ichrg_table[] = {   0,   85,  195,  293,  390,  488,  585,  683,
			  780,  878,  975, 1073, 1170, 1268, 1755, 9999 };

/* This table allows different max current limits and VCHRG settings based on
 * the current BP voltage.  For example, if BP is less than 3850mV, then 
 * the max current limit is 1000mA, and VCHRG is set to 4.25V.
 */

struct chrg_tbl_el {
	int	max_mV;
	int	max_mA;
	int	vchrg_value;
};

struct chrg_tbl_el  charging_table[] = { /* max mV, max current, vchrg */
#if 0
					  { 3800,	1000,	VCHRG_4250 },
					  { 3900,	 900,	VCHRG_4250 },
					  { 4000,	 800,	VCHRG_4250 },
					  { 4100,	 500,	VCHRG_4250 },
					  { 9999,	 300,	VCHRG_4200 }
#else
					  { 3800,	1000,	VCHRG_4200 },
					  { 3900,	1000,	VCHRG_4200 },
					  { 4000,	1000,	VCHRG_4200 },
					  { 4200,	1000,	VCHRG_4200 },
					  { 4230,	 900,	VCHRG_4200 },
					  { 9999,	 300,	VCHRG_4200 }
#endif
					};


static int determine_current_limit(int chrgraw, int bp, int power_max);
static void enable_lobathi_lobatli_interrupts(int enable_lobathi, int enable_lobatli);

static void charger_disable(void)
{
	printk(KERN_INFO "charger_disable\n");

	/* If charger has already been disabled, do not redo it */
	if (charger->charger_present == 0) {
		printk(KERN_INFO "Charger already disabled\n");
		return;
	}

	/* Deregister CPUFreq */
#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&charger->cpufreq_transition,
			CPUFREQ_TRANSITION_NOTIFIER);
#endif
	/* Delete the charger timer */
	del_timer_sync(&charger->timer);

	/* Cancel the work element */
	cancel_work_sync(&charger->charger_work);

	/* Turn off LED */
	set_led(LED_OFF);

	/* Clear the ichrg */
	pmic_write_reg(REG_CHARGER, 0, 0xffffffff);
	pmic_event_unsubscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);

	/* Charger state variables */
	charger->charger_present = 0;
	charger->current_led = LED_OFF;
	charger->current_ichrg = 0;
}

static void charger_enable(void)
{
	t_sensor_bits   sensors;
	pmic_get_sensors(&sensors);

	/* First check if the charger has already been enabled */
	if (charger->charger_present == 1) {
		printk(KERN_INFO " Charger already enabled\n");
		return;
	}

	printk(KERN_INFO "charger_enable\n");

	/* Only enable if a charger is indeed connected */
	if (sensors.sense_usb4v4s ||
		(sensors.sense_chgcurrs && sensors.sense_chgdets)) {	

		charger->charger_present = 1;

		/* Set the timer for 10 second default */	
		mod_timer(&charger->timer, jiffies + (charger->timer_delay * HZ));
		pmic_event_subscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);
		set_bit(OP_CHECK_CHARGER, &charger->operations);

		/* Queue the work element */
		queue_work(charger->charger_wq, &charger->charger_work);

		/* Re-register for CPUFreq */
#ifdef CONFIG_CPU_FREQ
		charger->cpufreq_transition.notifier_call = charger_cpufreq_notifier;
		cpufreq_register_notifier(&charger->cpufreq_transition,
				CPUFREQ_TRANSITION_NOTIFIER);
#endif
	}
}

static void charger_set_state(int enable)
{
	if (enable == 0)
		charger_disable();
	else 
		charger_enable();
}

#ifdef CONFIG_CPU_FREQ
int charger_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	switch (val) {
	case CPUFREQ_PRECHANGE:
		atomic_set(&charger_in_scaling, 1);
		break;
	case CPUFREQ_POSTCHANGE:
		atomic_set(&charger_in_scaling, 0);
		queue_work(charger->charger_wq, &charger->charger_work);
		break;
	}
	
	return 0;
}
#endif /* CONFIG_CPU_FREQ */

/* These are the external API's that are available to the arcotg_udc driver to
 * control current draw over the USB bus.
 */

int charger_get_current_limit(void)
{
	int		retval;

	retval = charger->current_current_limit;

	return retval;
}

/*
 * called from suspend/resume only
 */
int charger_set_current_limit_pm(int new_limit)
{
	log_chrg_api_calls(CHRG_MSG_DBG_SET_CURRENT_LIMIT, new_limit);

	charger->charger_present = false;
	charger->usb_current_limit = new_limit;
	set_bit(OP_SET_CHARGER, &charger->operations);
	queue_work(charger->charger_wq, &charger->charger_work);

	return 0;
}

/*
 * called from interrupt handler only. No need to disable/enable irq's
 */
int charger_set_current_limit(int new_limit)
{
	log_chrg_api_calls(CHRG_MSG_DBG_SET_CURRENT_LIMIT, new_limit);

	charger->charger_present = false;
	charger->usb_current_limit = new_limit;
	set_bit(OP_SET_CHARGER, &charger->operations);
	queue_work(charger->charger_wq, &charger->charger_work);
	if (new_limit > 100)
		host_charging = 1;
	else
		host_charging = 0;

	return 0;
}

int charger_handle_charging(void)
{
	log_chrg_api_calls(CHRG_MSG_DBG_HANDLE_CHARGING);

	charger->charger_present = true;
	queue_work(charger->charger_wq, &charger->charger_work);
	host_charging = 0;
	
	return 0;
}

void charger_set_arcotg_callback(int (*callback)(void *, int), void *arg)
{
	unsigned long	flags;

	log_chrg_api_calls(CHRG_MSG_DBG_SET_CALLBACK, callback, arg);

	spin_lock_irqsave(&charger->lock, flags);

	if (callback) {
		charger->arcotg_callback = callback;
		charger->arcotg_arg = arg;
	} else {
		charger->arcotg_callback = NULL;
		charger->arcotg_arg = NULL;
		charger->arcotg_state = 0;
	}

	spin_unlock_irqrestore(&charger->lock, flags);
}

EXPORT_SYMBOL(charger_get_current_limit);
EXPORT_SYMBOL(charger_set_current_limit);
EXPORT_SYMBOL(charger_handle_charging);
EXPORT_SYMBOL(charger_set_arcotg_callback);

static int lookup_max_current_limit(int bp_voltage)
{
	int	i = 0;

	if ((bp_voltage < 0) || (bp_voltage > 9999)) {
		log_chrg_err(CHRG_MSG_ERROR_INVALID_BP_VOLTAGE, bp_voltage,
				__FUNCTION__);
		return 100;  /* play it safe - return a very low limit */
	}

	while (bp_voltage > charging_table[i].max_mV) {
		i++;
	}

	return charging_table[i].max_mA;
}


static int determine_current_limit(int chrgraw, int bp, int power_max)
{
	int	delta_v = chrgraw - bp;
	int	max_current = 0;
	int	theoretical_current;
	int	table_current_limit;

	if (bp > chrgraw) {
		log_chrg_warn(CHRG_MSG_WARN_BP_ABOVE_CHRGRAW, bp, chrgraw);
		max_current = 500;
	}

	if (bp < 3000) {
		log_chrg_warn(CHRG_MSG_WARN_BP_VERY_LOW, bp);
		max_current = 500;
	}

	if (chrgraw > 5900) {
		log_chrg_warn(CHRG_MSG_WARN_CHRGRAW_VERY_HIGH, chrgraw);
		max_current = 100;
	}

	if (max_current != 0) {
		log_chrg_calcs(CHRG_MSG_DBG_MAX_CURRENT_LIMITED, max_current);

		return max_current;
	}

	/* If we got this far, normal case - calculate the current limit */

	if (delta_v == 0)
		delta_v = 1;	// avoid division by zero

	/* power_max is defined in milliwatts, while delta_v is in millivolts,
	 * so to get max_current in milliamps, we need to multiply power_max
	 * by 1000.
	 *
	 * theoretical_current is the amount of current allowable to stay
	 * under the power dissipation limit.  However, as delta_v gets close
	 * to zero, the allowable current gets unreasonable - close to 1,000 A!
	 *
	 * Because of this, max_current is based on a limited version of
	 * theoretical_max.  We look up the limit in the charging_table, and
	 * cap max_current to the lesser of theoretical_max and the value from
	 * the charging_table.  To allow different charge profiles, the limit
	 * value can vary based on BP voltage.
	 */

	theoretical_current = (1000 * power_max) / delta_v;

	log_chrg_calcs(CHRG_MSG_DBG_THEORETICAL_CURRENT, power_max, delta_v,
			theoretical_current, chrgraw, bp);
	
	table_current_limit = lookup_max_current_limit(bp);

	log_chrg_calcs(CHRG_MSG_DBG_TABLE_CURRENT_LIMIT, table_current_limit);

	/* One additional safeguard: if the table value is greater than the
	 * overall system maximum current limit, we limit it to that value.
	 * This protects against a mixed-up charging_table.
	 */

	if (table_current_limit > charger->max_current_limit)
		table_current_limit = charger->max_current_limit;

	/* max_current = min(theoretical_max, table_current_limit) */

	if (theoretical_current > table_current_limit)
		max_current = table_current_limit;
	else
		max_current = theoretical_current;

	log_chrg_calcs(CHRG_MSG_DBG_MAX_CURRENT, max_current);

	return max_current;
}


static void set_charger(int ichrg, int vchrg)
{
	PMIC_STATUS	err;

	log_chrg_entry_args("(ichrg=%d, vchrg=%d)", ichrg, vchrg);

	if (ichrg == 0) {
		err = pmic_batt_disable_charger(BATT_MAIN_CHGR);
		if (err) {
			log_chrg_err(CHRG_MSG_ERROR_DISABLING_CHARGER, err);
		}

	} else {
		err = pmic_batt_enable_charger(BATT_MAIN_CHGR, vchrg, ichrg);
		if (err) {
			log_chrg_err(CHRG_MSG_ERROR_ENABLING_CHARGER, err);
		}
	}

	log_chrg_exit();
}


/* pmic initialization required for the green LED.
 * This allows "green_led_control" to simply turn it on and off.
 */
static PMIC_STATUS green_led_initialization(void)
{
	t_bklit_channel	channel		= BACKLIGHT_LED3;
	t_bklit_mode	mode		= BACKLIGHT_CURRENT_CTRL_MODE;
	unsigned char	current_level	= 0;
	unsigned char	duty_cycle	= 0;
	unsigned char	cycle_time	= 0;
	int		en_dis		= 0;
	unsigned int	abms		= 0;
	unsigned int	abr		= 0;
	PMIC_STATUS	err[8];

	log_chrg_entry();

	err[0] = pmic_bklit_tcled_master_enable();

	err[1] = pmic_bklit_set_mode(channel, mode);

	err[2] = pmic_bklit_set_current(channel, current_level);

	err[3] = pmic_bklit_set_dutycycle(channel, duty_cycle);

	err[4] = pmic_bklit_set_cycle_time(cycle_time);
	
	err[5] = pmic_bklit_set_boost_mode(en_dis);

	err[6] = pmic_bklit_config_boost_mode(abms, abr);
	
	err[7] = pmic_bklit_disable_edge_slow();

	if (err[0] || err[1] || err[2] || err[3] ||
	    err[4] || err[5] || err[6] || err[7]) {
		log_chrg_err(CHRG_MSG_ERROR_GREEN_LED_INIT,
				err[0], err[1], err[2], err[3],
				err[4], err[5], err[6], err[7]);
		return -1;
	} else {
		return 0;
	}
}


static PMIC_STATUS green_led_control(int on)
{
	t_bklit_channel	channel = BACKLIGHT_LED3;
	unsigned char	duty_cycle;
	unsigned char	current_level;
	PMIC_STATUS	err, retval;

	if (on) {
		current_level = 4;
		duty_cycle = 7;
	} else {
		current_level = 0;
		duty_cycle = 0;
	}

	retval = pmic_bklit_set_current(channel, current_level);

	if (retval) {
		log_chrg_err(CHRG_MSG_ERROR_GREEN_LED_SET_CURRENT, retval);
	}

	err = pmic_bklit_set_dutycycle(channel, duty_cycle);

	if (err) {
		log_chrg_err(CHRG_MSG_ERROR_GREEN_LED_SET_DUTY_CYCLE, err);
		if (!retval)
			retval = err;
	}

	return retval;
}


static void set_led(int led_state)
{
	PMIC_STATUS	ret_y = 0, ret_g = 0;

	log_chrg_entry_args("(led_state = %d)", led_state);

	if (led_state == LED_OFF) {

		ret_y = pmic_batt_led_control(0);
		ret_g = green_led_control(0);

	} else if (led_state == LED_YELLOW) {

		ret_y = pmic_batt_led_control(1);
		ret_g = green_led_control(0);

	} else if (led_state == LED_GREEN) {

		ret_y = pmic_batt_led_control(0);
		ret_g = green_led_control(1);
	}

	if (ret_y || ret_g) {
		log_chrg_err(CHRG_MSG_ERROR_SET_LED, ret_y, ret_g);
	}

	log_chrg_exit();
}


/*
 * Formula to convert the BATT and BP ADC reading to millivolts:
 *
 * 	v (mV) = (adc_voltage_reading * 2300)/1024 + 2400
 */

static int adc_batt_to_mV(unsigned short adc_voltage_reading)
{
	return ((adc_voltage_reading * 2300) / 1024) + 2400;
}


/*
 * Formula to convert the CHRGRAW ADC reading to millivolts:
 *
 * 	v (mV) = (adc_voltage_reading * 2300 * 5)/1024
 */

static int adc_chrgraw_to_mV(unsigned short adc_voltage_reading)
{
	return (adc_voltage_reading * 2300 * 5) / 1024;
}


/*
 * converting an ADC reading to milliamps uses the following formula:
 *
 * 	i (mA) = (adc_current_reading * 2875)/512,
 * 		 where adc_current_reading is two's-complement, and
 * 		 ranges from [+511, ..., 0, ..., -512]
 */

static int adc_to_mA(unsigned short adc_current_reading)
{
	if (adc_current_reading <= 511) {
		return (adc_current_reading * 2875) / 512;
	} else {
		return ((1024-adc_current_reading) * -2875) / 512;
	}
}

/*
 * Get the latest readings from PMIC ADC.
 * 
 * This is sensitive code as it can cause potential deadlocks in the 
 * pmic_adc_convert() function.
 *
 * We cannot keep PMIC charging events disabled for long. So, after 
 * every pmic_adc_convert() call, release the mutex and enable the 
 * events to catch any.
 */
static int get_latest_readings(int *chrgraw, int *bp, int *batt)
{
	unsigned short	adc_chrgraw, adc_bp, adc_batt;
	unsigned short	adc_chrg_current, adc_batt_current;
	int		valid = true;
	PMIC_STATUS	pstat;

	disable_irq(PMIC_IRQ);	
	mutex_lock(&charger_wq_mutex);		/* Enter critical region */
	del_timer_sync(&charger->timer);	/* Disable charger timer */

	pstat = pmic_adc_convert(CHARGE_VOLTAGE, &adc_chrgraw);
	if (pstat) {
		log_chrg_err(CHRG_MSG_ERROR_CHRGRAW_READ, pstat);
		valid = false;
	}

	pstat = pmic_adc_convert(APPLICATION_SUPPLY, &adc_bp);
	if (pstat) {
		log_chrg_err(CHRG_MSG_ERROR_BP_READ, pstat);
		valid = false;
	}

	pstat = pmic_adc_convert(BATTERY_VOLTAGE, &adc_batt);
	if (pstat) {
		log_chrg_err(CHRG_MSG_ERROR_BATT_READ, pstat);
		valid = false;
	}

	pstat = pmic_adc_convert(CHARGE_CURRENT, &adc_chrg_current);
	if (pstat) {
		log_chrg_err(CHRG_MSG_ERROR_CHRG_CURRENT_READ, pstat);
		adc_chrg_current = 0x200;
	}

	pstat = pmic_adc_convert(BATTERY_CURRENT, &adc_batt_current);
	if (pstat) {
		log_chrg_err(CHRG_MSG_ERROR_BATT_CURRENT_READ, pstat);
		adc_batt_current = 0x200;
	}

	mod_timer(&charger->timer, jiffies + (charger->timer_delay * HZ));
	mutex_unlock(&charger_wq_mutex);
	enable_irq(PMIC_IRQ);

	if (valid == false) {
		return false;
	}

	*chrgraw = adc_chrgraw_to_mV(adc_chrgraw);
	*bp	 = adc_batt_to_mV(adc_bp);
	*batt	 = adc_batt_to_mV(adc_batt);

	log_chrg_readings(CHRG_MSG_DBG_VOLTAGE_READINGS,
			*chrgraw, *bp, *batt,
			adc_chrgraw, adc_bp, adc_batt);
	log_chrg_readings(CHRG_MSG_DBG_CURRENT_READINGS,
			adc_to_mA(adc_chrg_current), adc_to_mA(adc_batt_current), 
			adc_chrg_current, adc_batt_current);

	return true;
}

static int lookup_vchrg(int bp_voltage)
{
	int	i = 0;

	if ((bp_voltage < 0) || (bp_voltage > 9999)) {
		log_chrg_err(CHRG_MSG_ERROR_INVALID_BP_VOLTAGE, bp_voltage,
				__FUNCTION__);
		return VCHRG_4200;
	}

	while (bp_voltage > charging_table[i].max_mV) {
		i++;
	}

	return charging_table[i].vchrg_value;
}


static int lookup_ichrg(int mA)
{
	int	i = 0;

	log_chrg_entry_args("(mA = %d)", mA);

	/* We walk the table looking for the largest value that does not
	 * exceed the requested current limit.  When we find a value that
	 * exceeds the requested current limit, we know the previous value
	 * was the largest acceptable value.
	 * The ICHRG value is equal to the array index value.
	 */
	
	if ((mA < 0) || (mA > ichrg_table[15])) {
		log_chrg_err(CHRG_MSG_ERROR_INVALID_MILLIAMPS, mA);
		return 1;	/* small charge current: ~70 mA */
	}

	while (ichrg_table[i+1] < mA) {
		i++;
	}

	log_chrg_calcs(CHRG_MSG_DBG_ICHRG_CALCS, mA, i, i, ichrg_table[i]);

	return i;
}


static void post_low_battery_event(void)
{
	struct kobject *kobj = &charger->dev.dev.kobj;

	if (kobject_uevent_env(kobj, KOBJ_CHANGE, envp_low)) {
		log_chrg_err(CHRG_MSG_ERROR_POSTING_LOW_BATT_EVT);
	} else {
		log_chrg_info(CHRG_MSG_INFO_POSTED_LOW_BATT_EVT);
	}
}

static void post_critical_battery_event(void)
{
	struct kobject *kobj = &charger->dev.dev.kobj;

	if (kobject_uevent_env(kobj, KOBJ_CHANGE, envp_critical)) {
		log_chrg_err(CHRG_MSG_ERROR_POSTING_CRIT_BATT_EVT);
	} else {
		log_chrg_info(CHRG_MSG_INFO_POSTED_CRIT_BATT_EVT);
	}
}

static void charger_arcotg_wakeup(struct work_struct *not_used)
{
	queue_work(charger->charger_wq, &charger->charger_work);
}

extern int mxc_spi_suspended;

static void charger_work_fn(struct work_struct *work)
{
	int		current_limit = 0;
	int		vbus_present = 0, charger_present = 0;
	int		max_dissipation = 0;
	int		temp_ok = 0, battery_full = 0, usb_current_limit = 0;
	unsigned long	ops = 0;
	int		chrgraw = 0, bp = 0, batt = 0;
	int		lobathi_occurred = 0;
	int		lobatli_occurred = 0;
	int		critical_voltage = 0, low_voltage = 0;
	int		new_current_limit = 0, new_ichrg = 0, new_vchrg = 0, new_led = 0;
	int		need_to_set_charger = 0, need_to_set_led = 0;
	int		valid = 0;
	int		ret = 0; /* Check return value from arcotg */
	t_sensor_bits	sensors;

	if (atomic_read(&charger_in_scaling) == 1) {
		queue_work(charger->charger_wq, &charger->charger_work);
		return;
	}	

	if (mxc_spi_suspended) {
		queue_work(charger->charger_wq, &charger->charger_work);
		return;
	}

	log_chrg_entry();

	if (charger->suspended)
		goto exit_charger;

	pmic_get_sensors(&sensors);
	if (sensors.sense_usb4v4s || (sensors.sense_chgcurrs && sensors.sense_chgdets)) {
		/* Check battery error flags as well */
		if ( (charger->current_led == LED_OFF) && 
			(battery_error_flags == 0) ) {
			schedule_delayed_work(&charger_usb4v4_work, msecs_to_jiffies(1000));
		}
	}
			
	ops = charger->operations;
	charger->operations = 0;
	atomic_set(&arcotg_callback_done, 0);

	/* First, handle any interrupt enabling/disabling.  This may be a high
	 * priority - for example, in response to a LOBATLI while running.
	 * Under normal conditions, LOBATLI is enabled, while LOBATHI is not.
	 *
	 * So there are 4 cases (listed in order of priority):
	 *
	 * 				LOBATHI		LOBATLI
	 * 				--------	--------
	 * Low-battery (hi and/or lo)	disabled	disabled
	 *
	 * Off				disabled	disabled
	 *
	 * Suspended			enabled		enabled
	 *
	 * Normal (run/after resume)	disabled	enabled
	 *
	 * NOTE:  Since the PMIC code is clever, and leaves the interrupt
	 * unmasked as long as a callback is registered for a given source,
	 * we need to unsubscribe our callback to mask the interrupt.
	 */

	if (test_bit(OP_DISABLE_INTR, &ops)) {
		enable_lobathi_lobatli_interrupts(false, false);
	}

	/* Next, handle the USB plug-in event, if necessary.  */
	if (test_bit(OP_PLUG_INSERTION, &ops)) {
		if (charger->arcotg_callback) {
			ret = charger->arcotg_callback(charger->arcotg_arg,
						charger->arcotg_state);

			if (ret == -EAGAIN) {
				/*
				 * arcotg is not ready yet. So, we will need to
				 * set this bit. We can then either leave it upto
				 * the charger timer that expires after 10 seconds
				 * or queue work here. Queue'ing work makes sense
				 * since it will be faster.
				 */
				charger->operations |= ops;
				schedule_delayed_work(&charger_arcotg_work, msecs_to_jiffies(5000));	
				set_led(LED_YELLOW);	
				goto exit;
			}

			if (ret == -EBUSY) {
				charger->operations |= ops;
				schedule_delayed_work(&charger_arcotg_work, msecs_to_jiffies(1000));
				set_led(LED_YELLOW);
				goto exit;
			}
			atomic_set(&arcotg_callback_done, 1);
		}
	}

	/*
	 * We're going to need the latest readings for all our calculations
	 * Make sure the arcotg is not in a misdetect retry else it's
	 * timers will fire while we read the ADC
	 */
	if (charger_misdetect_retry == 0)
		valid = get_latest_readings(&chrgraw, &bp, &batt);

	if (valid) {
		charger->voltage = bp;		/* save latest readings */
		charger->vbatt	 = batt;

		if (chrgraw >= bp) {
			charger->vbus_present = 1;
		} else {
			charger->vbus_present = 0;
			if (chrgraw > (bp >> 4)) {
				log_chrg_warn(CHRG_MSG_WARN_UNEXPECTED_READING,
						chrgraw, bp, batt);
			}
		}
	} else {
		bp   = charger->voltage;  /* use last known good readings */
		batt = charger->vbatt;
	}

	temp_ok = charger->temp_ok;
	vbus_present = charger->vbus_present;
	usb_current_limit = charger->usb_current_limit;
	battery_full = charger->battery_full;
	current_limit = charger->current_current_limit;
	charger_present = charger->charger_present;
	max_dissipation = charger->max_power_dissipation;

	/* The LOBATH (and possibly LOBATL) interrupt may have been spurious.
	 * We need to check the current battery level to decide whether or not
	 * to send an event notification to the system.
	 *
	 * If the current BP voltage is above 3.4V (the low battery threshold),
	 * we will consider the interrupt as spurious.
	 */

	lobathi_occurred = test_bit(OP_LOBATHI, &ops);
	lobatli_occurred = test_bit(OP_LOBATLI, &ops);
	critical_voltage = (bp <= CRITICAL_VOLTAGE_THRESHOLD);
	low_voltage	 = (bp <= LOW_VOLTAGE_THRESHOLD);

	if (lobathi_occurred || lobatli_occurred || critical_voltage) {

		log_chrg_info(CHRG_MSG_INFO_LOW_BATT_READINGS, bp, batt);
		
		if (bp > LOW_VOLTAGE_THRESHOLD) {
			log_chrg_warn(CHRG_MSG_WARN_SPURIOUS_INTERRUPT,
					(lobathi_occurred ? 1 : 0),
					(lobatli_occurred ? 1 : 0));

			enable_lobathi_lobatli_interrupts(false, true);
		} else {
			if (lobatli_occurred || critical_voltage) {
				post_critical_battery_event();
			} else if (lobathi_occurred) {
				post_low_battery_event();
			}
		}
	}

	/* Handle the charger control work */

	if (!temp_ok || !vbus_present || (battery_error_flags != 0)) {
	
		/* powerd will inform us if the temperature is out of range
		 * for charging.  If this is the case, we have to turn off
		 * the charge regulator to stop charging the battery.
		 * Also, if VBUS has gone away, we need to shut down the
		 * charge regulator as well.
		 */
		log_chrg_calcs(CHRG_MSG_DBG_TEMP_AND_VBUS, temp_ok,
				vbus_present, battery_error_flags);

		new_current_limit = 0;
		new_ichrg = 0;
		new_vchrg = lookup_vchrg(bp);
		new_led   = LED_OFF;

	} else {
		if (charger_present) {
			new_current_limit = determine_current_limit(chrgraw,
						bp, max_dissipation);
		} else {
			new_current_limit = usb_current_limit;
		}

		new_ichrg = lookup_ichrg(new_current_limit);
		new_vchrg = lookup_vchrg(bp);

		if (new_ichrg == 0)
			new_led = LED_OFF;
		else if (battery_full)
			new_led = LED_GREEN;
		else
			new_led = LED_YELLOW;
	}

	if ((charger->current_current_limit != new_current_limit) ||
			(charger->current_ichrg != new_ichrg) ||
			(charger->current_vchrg != new_vchrg))
		need_to_set_charger = true;
	else
		need_to_set_charger = false;

	if (charger->current_led != new_led) {
		need_to_set_led = true;
		log_chrg_info(CHRG_MSG_INFO_CHARGE_LED_CHANGE,
				charge_led_string[charger->current_led],
				charge_led_string[new_led],
				charge_led_string[charger->current_led],
				charge_led_string[new_led]);
	} else
		need_to_set_led = false;

	if (need_to_set_led) {
		if (atomic_read(&arcotg_callback_done) == 0)
			schedule_delayed_work(&charger_usb4v4_work, msecs_to_jiffies(1000));
	}

	if (need_to_set_charger || need_to_set_led) {
		charger->current_current_limit = new_current_limit;
		charger->current_ichrg = new_ichrg;
		charger->current_vchrg = new_vchrg;
		charger->current_led   = new_led;

		if (need_to_set_charger)
			set_charger(new_ichrg, new_vchrg);

		if (need_to_set_led)
			set_led(new_led);
	}
exit:	
	/* If a request was received to switch to the appropriate interrupt
	 * enables for suspend, then we're in the middle of handling a suspend
	 * request.
	 *
	 * Before changing the interrupt enables, check if BP is already below
	 * the LOBATHI threshold.  If it is, don't bother suspending - just
	 * post the low battery event and refuse to suspend.
	 *
	 * (Setting charger->suspended to true tells the suspend handler to
	 *  allow the system suspend to proceed.  If it is false, then the
	 *  suspend handler should return an error result to stop the suspend
	 *  process.)
	 */
	if (test_bit(OP_ENABLE_SUSPEND_INTR, &ops)) {
		test_and_clear_bit(OP_ENABLE_SUSPEND_INTR, &charger->operations);

		if (bp <= CRITICAL_VOLTAGE_THRESHOLD) {
			post_critical_battery_event();
			charger->suspended = false;
		} else if (bp <= LOW_VOLTAGE_THRESHOLD) {
			post_low_battery_event();
			charger->suspended = false;  // stop suspend process
		} else {
			enable_lobathi_lobatli_interrupts(true, true);
			charger->suspended = true;   // allow suspend
		}


	} else if (test_bit(OP_ENABLE_NORMAL_INTR, &ops)) {
		test_and_clear_bit(OP_ENABLE_NORMAL_INTR, &charger->operations);
		enable_lobathi_lobatli_interrupts(false, true);
	}
exit_charger:
	log_chrg_exit();
}


static void enable_lobathi_lobatli_interrupts(int enable_lobathi, int enable_lobatli)
{
	unsigned long	flags;
	PMIC_STATUS	retval;

	log_chrg_entry_args("(enable_lobathi = %d, enable_lobatli = %d)",
			enable_lobathi, enable_lobatli);

	if (enable_lobathi) {
		retval = pmic_event_subscribe(EVENT_LOBATHI,
				charger->lobathi_callback);
		if (retval) {
			log_chrg_err(CHRG_MSG_ERROR_SUBSCRIBE_LOBATHI, retval);
		} else {
			log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT_CTRL,
					"LOBATHI enabled");

			spin_lock_irqsave(&charger->lock, flags);
			charger->lobathi_callback_subscribed = CALLBACK_SUBSCRIBED;
			spin_unlock_irqrestore(&charger->lock, flags);
		}
	} else {
		retval = pmic_event_unsubscribe(EVENT_LOBATHI,
				charger->lobathi_callback);
		if (retval) {
			log_chrg_err(CHRG_MSG_ERROR_UNSUBSCRIBE_LOBATHI, retval);
		} else {
			log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT_CTRL,
					"LOBATHI disabled");
			spin_lock_irqsave(&charger->lock, flags);
			charger->lobathi_callback_subscribed = CALLBACK_NOT_SUBSCRIBED;
			spin_unlock_irqrestore(&charger->lock, flags);
		}
	}

	if (enable_lobatli) {
		retval = pmic_event_subscribe(EVENT_LOBATLI,
				charger->lobatli_callback);
		if (retval) {
			log_chrg_err(CHRG_MSG_ERROR_SUBSCRIBE_LOBATLI, retval);
		} else {
			log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT_CTRL,
					"LOBATLI enabled");
			spin_lock_irqsave(&charger->lock, flags);
			charger->lobatli_callback_subscribed = CALLBACK_SUBSCRIBED;
			spin_unlock_irqrestore(&charger->lock, flags);
		}
	} else {
		retval = pmic_event_unsubscribe(EVENT_LOBATLI,
				charger->lobatli_callback);
		if (retval) {
			log_chrg_err(CHRG_MSG_ERROR_UNSUBSCRIBE_LOBATLI, retval);
		} else {
			log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT_CTRL,
					"LOBATLI disabled");
			spin_lock_irqsave(&charger->lock, flags);
			charger->lobatli_callback_subscribed = CALLBACK_NOT_SUBSCRIBED;
			spin_unlock_irqrestore(&charger->lock, flags);
		}
	}

	log_chrg_exit();
}


static void charger_timer(unsigned long data)
{
	log_chrg_entry();

	set_bit(OP_CHECK_CHARGER, &charger->operations);
	queue_work(charger->charger_wq, &charger->charger_work);
	mod_timer(&charger->timer, jiffies + (charger->timer_delay * HZ));

	log_chrg_exit();
}


/* Battery status change notification handler */
static int charger_batt_err_callback(void * arg, int error_flags)
{
	log_chrg_info(CHRG_MSG_INFO_BATT_CALLBACK, error_flags);
	queue_work(charger->charger_wq, &charger->charger_work);
	return 0;
}


/*
 * Atlas event handlers
 */
static void charger_queue_plug_event(int state)
{
	charger->arcotg_state = state;
	if (state)
		charger->vbus_present = 1;

	set_bit(OP_PLUG_INSERTION, &charger->operations);
	queue_work(charger->charger_wq, &charger->charger_work);
}

static void chgdeti_event(void *param)
{
	t_sensor_bits	sensors;

	disable_irq(PMIC_IRQ);
	mutex_lock(&charger_wq_mutex);

	pmic_get_sensors(&sensors);

	if (!sensors.sense_chgdets) {
		mutex_unlock(&charger_wq_mutex);
		enable_irq(PMIC_IRQ);
		return;
	}

	log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT,
			"Charger Detect interrupt (CHGDETI)");

	charger_queue_plug_event(1);
	mutex_unlock(&charger_wq_mutex);
	enable_irq(PMIC_IRQ);
}

static void charger_usb4v4(struct work_struct *not_used)
{
	t_sensor_bits   sensors;

	disable_irq(PMIC_IRQ);

	if (charger_turn_off_curri == 1) {
		pmic_event_unsubscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);
		charger_turn_off_curri = 0;
		curri_test = 1;
	}

	mutex_lock(&charger_wq_mutex);

	pmic_get_sensors(&sensors);
	if (sensors.sense_usb4v4s || (sensors.sense_chgcurrs && sensors.sense_chgdets)) {
		if (curri_test == 1) {
			schedule_delayed_work(&charger_usb4v4_work, msecs_to_jiffies(3000));
		}
		if (charger->current_led == LED_OFF) {
			charger_queue_plug_event(1);
		}
	}
	else {
		curri_test = 0;
		set_led(LED_OFF);
		charger_queue_plug_event(0);
		pmic_write_reg(REG_CHARGER, 0, 0xffffffff);
		pmic_event_subscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);
	}

	mutex_unlock(&charger_wq_mutex);
	enable_irq(PMIC_IRQ);
}

static void chrgcurri_event(void *param)
{
	t_sensor_bits   sensors;

	disable_irq(PMIC_IRQ);

	log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT,
			"Charge current below threshold intr (CHRGCURRI)");

	if (charger_turn_off_curri == 1) {
		enable_irq(PMIC_IRQ);
		return;
	}

	mutex_lock(&charger_wq_mutex);

	pmic_get_sensors(&sensors);
	if (sensors.sense_usb4v4s || (sensors.sense_chgcurrs && sensors.sense_chgdets)) {
		charger_turn_off_curri = 1;
		schedule_delayed_work(&charger_usb4v4_work, msecs_to_jiffies(10));
	}
	else {
		/* 1 second debounce time */
		schedule_delayed_work(&charger_usb4v4_work, msecs_to_jiffies(1000));
	}

	mutex_unlock(&charger_wq_mutex);
	enable_irq(PMIC_IRQ);
}


/* When we hit these thresholds, we want to either suspend or
 * shut down the system.
 *
 * These interrupts are generated continuously when BP is below
 * the thresholds, so we need to unsubscribe these handlers in
 * order to disable this interrupt source when it occurs.
 */
static void lobathi_event(void *param)
{
	int reg;
	t_sensor_bits sensors;

	log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT,
			"Low battery warning interrupt (LOBATHI)");

	if (charger->lobathi_callback_subscribed == CALLBACK_NOT_SUBSCRIBED) {
		pmic_read_reg(REG_INTERRUPT_MASK_0, &reg, 0xffffffff);

		log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT_CTRL,
			"LOBATHI occurred after unsubscription");

		pmic_get_sensors(&sensors);

		printk(KERN_ERR "charger: E def:332:reg=%x,LOBATHS=%d,LOBATHL=%d\n",
					reg, sensors.sense_lobaths, sensors.sense_lobatls);
	}

	set_bit(OP_LOBATHI, &charger->operations);

	set_bit(OP_DISABLE_INTR, &charger->operations);

	queue_work(charger->charger_wq, &charger->charger_work);
}

static void lobatli_event(void *param)
{
	log_chrg_interrupts(CHRG_MSG_DBG_INTERRUPT,
			"Critical battery interrupt (LOBATLI)");

	set_bit(OP_LOBATLI, &charger->operations);

	set_bit(OP_DISABLE_INTR, &charger->operations);

	queue_work(charger->charger_wq, &charger->charger_work);
}


/*
 * sysfs entries
 */

/* "battery_full" sysfs attribute */

static ssize_t
show_battery_full (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->battery_full);
}

static ssize_t
store_battery_full (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int	value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((value == 0) || (value == 1))) {
		charger->battery_full = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (battery_full, S_IRUGO|S_IWUSR, show_battery_full,
		store_battery_full);


/* "charging" sysfs attribute */

static ssize_t
show_charging (struct device *_dev, struct device_attribute *attr, char *buf)
{
	int	charging = 0;

	if (charger->charger_present || (charger->current_ichrg > 1))
		charging = 1;
	
	return scnprintf(buf, PAGE_SIZE, "%d\n", charging);
}

static DEVICE_ATTR (charging, S_IRUGO, show_charging, NULL);


/* "current_limit" sysfs attribute */

static ssize_t
show_current_limit (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			charger->current_current_limit);
}

static DEVICE_ATTR (current_limit, S_IRUGO, show_current_limit, NULL);


/* "errors" sysfs attribute - nonzero indicates some battery-related issue */

static ssize_t
show_errors (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			battery_error_flags);
}

static DEVICE_ATTR (errors, S_IRUGO, show_errors, NULL);


/* "globals" sysfs attribute */

static ssize_t
show_globals (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			"cwq:%d op:%lu lcs:%d hcs:%d sus:%d vbp:%d chp:%d\n"
			"ccl:%d ci:%d cv:%d cl:%d ucl:%d mcl:%d mpd:%d\n"
			"bp:%d vb:%d tok:%d bfl:%d ast:%d\n",
			charger->charger_work_queued, charger->operations,
			charger->lobatli_callback_subscribed,
			charger->lobathi_callback_subscribed,
			charger->suspended,
			charger->vbus_present, charger->charger_present,
			charger->current_current_limit,
			charger->current_ichrg, charger->current_vchrg,
			charger->current_led,
			charger->usb_current_limit,
			charger->max_current_limit,
			charger->max_power_dissipation,
			charger->voltage, charger->vbatt,
			charger->temp_ok, charger->battery_full,
			charger->arcotg_state);
}

static DEVICE_ATTR (globals, S_IRUGO, show_globals, NULL);

/* "ichrg_setting" sysfs attribute */

static ssize_t
show_ichrg_setting (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->current_ichrg);
}

static DEVICE_ATTR (ichrg_setting, S_IRUGO, show_ichrg_setting, NULL);


/* "logging" sysfs attribute */

static ssize_t
show_logging (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", LLOG_G_LOG_MASK);
}

static ssize_t
store_logging (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int	value;

	if (sscanf(buf, "%x", &value) > 0) {
		LLOG_G_LOG_MASK = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (logging, S_IRUGO|S_IWUSR, show_logging, store_logging);


/* "max_power" sysfs attribute */

static ssize_t
show_max_power (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->max_power_dissipation);
}

static ssize_t
store_max_power (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int	value;

	if ((sscanf(buf, "%d", &value) > 0) && (value > 0)) {
		charger->max_power_dissipation = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (max_power, S_IRUGO|S_IWUSR, show_max_power, store_max_power);

/* charger enable/disable on the fly */
static ssize_t
show_charger_state(struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->charger_present);
}

static ssize_t
store_charger_state (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((value == 0) || (value == 1))) {
		charger_set_state(value);
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (charger_state, S_IRUGO|S_IWUSR, show_charger_state, store_charger_state);

/* "temp_ok" sysfs attribute */

static ssize_t
show_temp_ok (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->temp_ok);
}

static ssize_t
store_temp_ok (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int	value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((value == 0) || (value == 1))) {
		charger->temp_ok = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (temp_ok, S_IRUGO|S_IWUSR, show_temp_ok, store_temp_ok);


/* "timer_delay" sysfs attribute */

static ssize_t
show_timer_delay (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->timer_delay);
}

static ssize_t
store_timer_delay (struct device *_drvr, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int	value;

	if ((sscanf(buf, "%d", &value) > 0) && (value > 0)) {
		charger->timer_delay = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (timer_delay, S_IRUGO|S_IWUSR, show_timer_delay, store_timer_delay);


/* "vbatt" sysfs attribute */

static ssize_t
show_vbatt (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->vbatt);
}

static DEVICE_ATTR (vbatt, S_IRUGO, show_vbatt, NULL);


/* "voltage" sysfs attribute */

static ssize_t
show_voltage (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->voltage);
}

static DEVICE_ATTR (voltage, S_IRUGO, show_voltage, NULL);


/*
 * Driver infrastructure calls - probe/remove, suspend/resume
 */

static int charger_suspend(struct platform_device *pdev, pm_message_t state)
{
	int	retval;

	log_chrg_entry_args("(state.event = %d)", state.event);

	del_timer_sync(&charger->timer);

	/* Enable the appropriate low-battery interrupts for suspend mode,
	 * in order to wake the device to shut it down.
	 */

	set_bit(OP_ENABLE_SUSPEND_INTR, &charger->operations);

	mutex_lock(&charger_wq_mutex);
	charger_set_current_limit_pm(0);	/* will queue up work queue element */
	mutex_unlock(&charger_wq_mutex);

	flush_workqueue(charger->charger_wq);	/* run the work queue */

	/* The work queue element will set charger->suspended to true if it
	 * is okay to complete suspending.  If charger->suspended is false,
	 * then prevent the system from suspending.
	 */

	if (charger->suspended)
		retval = 0;
	else
		retval = -EBUSY;

	log_chrg_exit();

	return retval;
}

static int charger_resume(struct platform_device *pdev)
{
	log_chrg_entry();

	charger->suspended = false;

	/* The charger LED will be on if the user resumed the device
	 * by connecting USB.  Assuming the state is "on" helps work
	 * around an edge case if the charger needs to be forced off.
	 * (SW thinks the LED is off, so it doesn't turn it off.)
	 */
	charger->current_led = 1;

	green_led_initialization();

	mod_timer(&charger->timer, jiffies + (charger->timer_delay * HZ));
	
	set_bit(OP_CHECK_CHARGER, &charger->operations);

	/* Go back to normal low-battery interrupt enables:  we only want an
	 * interrupt when the battery voltage spikes so low that we should
	 * shut down.
	 */

	set_bit(OP_ENABLE_NORMAL_INTR, &charger->operations);

	mutex_lock(&charger_wq_mutex);
	charger_set_current_limit_pm(100);	/* will queue up work queue element */
	mutex_unlock(&charger_wq_mutex);

	flush_workqueue(charger->charger_wq);

	log_chrg_exit();

	return 0;
}

static int charger_probe(struct platform_device *pdev)
{
	log_chrg_entry();
	log_chrg_exit();
	return 0;
}

static int charger_remove(struct platform_device *pdev)
{
	log_chrg_entry();
	log_chrg_exit();
	return 0;
}


static void charger_device_release(struct device *dev)
{
	log_chrg_entry();
	log_chrg_exit();
}


static int __init charger_init(void)
{
	int		retval;
	t_sensor_bits	sensors;
	int		connected;

	LLOG_INIT();
	LLOG_G_LOG_MASK |= CHRG_LOG_DEFAULT;

	log_chrg_info(CHRG_MSG_INFO_DRIVER_LOADING, DRIVER_VERSION);

	spin_lock_init(&charger->lock);

	charger->charger_wq = create_workqueue("charger");
	
	if (!charger->charger_wq) {
		log_chrg_err(CHRG_MSG_ERROR_CREATING_WORKQUEUE);
		retval = -ENOMEM;
		goto exit;
	}
	
	INIT_WORK(&charger->charger_work, charger_work_fn);
	charger->charger_work_queued = false;

	charger->timer_delay = DEFAULT_TIMER_DELAY; /* seconds */

	init_timer(&charger->timer);
	charger->timer.expires = jiffies + (charger->timer_delay * HZ);
	charger->timer.data = (unsigned long) charger;
	charger->timer.function = charger_timer;
	
	charger->arcotg_callback = NULL;
	charger->arcotg_arg = NULL;

	/* Create a fake piece of hardware for us to bind against... */

	retval = platform_device_register(&charger->dev);
	
	if (retval) {
		log_chrg_err(CHRG_MSG_ERROR_REGISTERING, retval,
				"platform_device_register");
		goto exit;
	}

	retval = platform_driver_register(&charger->drvr);

	if (retval) {
		log_chrg_err(CHRG_MSG_ERROR_REGISTERING, retval,
				"platform_driver_register");
		goto exit1;
	}

	add_timer(&charger->timer);

	retval = device_create_file(&charger->dev.dev, &dev_attr_battery_full);
	retval = device_create_file(&charger->dev.dev, &dev_attr_charging);
	retval = device_create_file(&charger->dev.dev, &dev_attr_current_limit);
	retval = device_create_file(&charger->dev.dev, &dev_attr_errors);
	retval = device_create_file(&charger->dev.dev, &dev_attr_globals);
	retval = device_create_file(&charger->dev.dev, &dev_attr_ichrg_setting);
	retval = device_create_file(&charger->dev.dev, &dev_attr_logging);
	retval = device_create_file(&charger->dev.dev, &dev_attr_max_power);
	retval = device_create_file(&charger->dev.dev, &dev_attr_temp_ok);
	retval = device_create_file(&charger->dev.dev, &dev_attr_timer_delay);
	retval = device_create_file(&charger->dev.dev, &dev_attr_vbatt);
	retval = device_create_file(&charger->dev.dev, &dev_attr_voltage);
	retval = device_create_file(&charger->dev.dev, &dev_attr_charger_state);

	battery_register_callback(charger_batt_err_callback, NULL);

	charger->chgdeti_callback.func    = chgdeti_event;
	charger->chgdeti_callback.param   = NULL;
	charger->chrgcurri_callback.func  = chrgcurri_event;
	charger->chrgcurri_callback.param = NULL;
	charger->lobathi_callback.func    = lobathi_event;
	charger->lobathi_callback.param   = NULL;
	charger->lobatli_callback.func    = lobatli_event;
	charger->lobatli_callback.param   = NULL;

	pmic_event_subscribe(EVENT_CHGDETI, charger->chgdeti_callback);
	pmic_event_subscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);
	charger->lobathi_callback_subscribed = CALLBACK_NOT_SUBSCRIBED;
	//pmic_event_subscribe(EVENT_LOBATHI, charger->lobathi_callback);
	charger->lobatli_callback_subscribed = CALLBACK_SUBSCRIBED;
	pmic_event_subscribe(EVENT_LOBATLI, charger->lobatli_callback);
	
	retval = green_led_initialization();

	/*
	 * Check status of USB4V4
	 */
	pmic_get_sensors(&sensors);
	connected = sensors.sense_usb4v4s ||
			(sensors.sense_chgcurrs && sensors.sense_chgdets);
	
	if (connected && charger->temp_ok && (battery_error_flags == 0))
		charger_queue_plug_event(1);
	else {
		set_led(LED_OFF);
		pmic_write_reg(REG_CHARGER, 0, 0xffffffff);
	}


#ifdef CONFIG_CPU_FREQ
	charger->cpufreq_transition.notifier_call = charger_cpufreq_notifier;
	cpufreq_register_notifier(&charger->cpufreq_transition,
					CPUFREQ_TRANSITION_NOTIFIER);
#endif

	charger_probed = 1;
	goto exit;
exit1:
	platform_device_unregister(&charger->dev);
exit:
	return retval;
}

static void __exit charger_exit(void)
{
	del_timer_sync(&charger->timer);

	battery_register_callback(NULL, NULL);

	pmic_event_unsubscribe(EVENT_CHGDETI, charger->chgdeti_callback);
	pmic_event_unsubscribe(EVENT_CHRGCURRI, charger->chrgcurri_callback);
	if (charger->lobathi_callback_subscribed)
		pmic_event_unsubscribe(EVENT_LOBATHI, charger->lobathi_callback);
	if (charger->lobatli_callback_subscribed)
		pmic_event_unsubscribe(EVENT_LOBATLI, charger->lobatli_callback);

	cancel_work_sync(&charger->charger_work);
	flush_workqueue(charger->charger_wq);
	destroy_workqueue(charger->charger_wq);

	device_remove_file(&charger->dev.dev, &dev_attr_battery_full);
	device_remove_file(&charger->dev.dev, &dev_attr_charging);
	device_remove_file(&charger->dev.dev, &dev_attr_current_limit);
	device_remove_file(&charger->dev.dev, &dev_attr_errors);
	device_remove_file(&charger->dev.dev, &dev_attr_globals);
	device_remove_file(&charger->dev.dev, &dev_attr_ichrg_setting);
	device_remove_file(&charger->dev.dev, &dev_attr_logging);
	device_remove_file(&charger->dev.dev, &dev_attr_max_power);
	device_remove_file(&charger->dev.dev, &dev_attr_temp_ok);
	device_remove_file(&charger->dev.dev, &dev_attr_timer_delay);
	device_remove_file(&charger->dev.dev, &dev_attr_vbatt);
	device_remove_file(&charger->dev.dev, &dev_attr_voltage);
	device_remove_file(&charger->dev.dev, &dev_attr_charger_state);

	platform_device_unregister(&charger->dev);
	
	platform_driver_unregister(&charger->drvr);

	log_chrg_info("unld", "", " Battery charger driver unloaded.\n");

#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&charger->cpufreq_transition,
				CPUFREQ_TRANSITION_NOTIFIER);
#endif
	charger_probed = 0;
}

module_init(charger_init);
module_exit(charger_exit);

MODULE_DESCRIPTION("Charger driver");
MODULE_AUTHOR("Jerry Katzung");
MODULE_AUTHOR("Manish Lachwani");
MODULE_LICENSE("GPL");
