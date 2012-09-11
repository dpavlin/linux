/*
 * charger.h - Battery charger driver
 *
 * Copyright 2008 Lab126, Inc.
 */

/* Uncomment these defines if extreme logging is necessary */

//#define CHRG_LOG_VERBOSE	/* Turn up verbosity automatically */
//#define CHRG_LOG_LOUD		/* Promote DEBUG messages to ALERT */

/* Bit numbers for "operation" field */

#define OP_CHECK_CHARGER		0	/* normal charger monitoring */
#define OP_SET_CHARGER			1	/* request from USB driver */

#define OP_ENABLE_NORMAL_INTR		4
#define OP_ENABLE_SUSPEND_INTR		5
#define OP_DISABLE_INTR			6

#define OP_LOBATLI			8
#define OP_LOBATHI			9

#define OP_PLUG_INSERTION		12


/* pmic callback information */

#define CALLBACK_NOT_SUBSCRIBED		0
#define CALLBACK_SUBSCRIBED		1


/* LED states */

#define LED_OFF		0	/* Not connected or charging */
#define LED_YELLOW	1	/* Charging, but not full */
#define LED_GREEN	2	/* Full */


/* Logging control */

#define CHRG_MSK_READINGS   LLOG_LEVEL_DEBUG0  /* record voltages/currents */
#define CHRG_MSK_CALCS	    LLOG_LEVEL_DEBUG1  /* log current limit math */
#define CHRG_MSK_API_CALLS  LLOG_LEVEL_DEBUG2  /* record calls from clients */
#define CHRG_MSK_INTERRUPTS LLOG_LEVEL_DEBUG3  /* MC13783 interrupts */
#define CHRG_MSK_UNUSED4    LLOG_LEVEL_DEBUG4
#define CHRG_MSK_UNUSED5    LLOG_LEVEL_DEBUG5
#define CHRG_MSK_UNUSED6    LLOG_LEVEL_DEBUG6
#define CHRG_MSK_ENTRY_EXIT LLOG_LEVEL_DEBUG7  /* call chain info */
#define CHRG_MSK_UNUSED8    LLOG_LEVEL_DEBUG8
#define CHRG_MSK_UNUSED9    LLOG_LEVEL_DEBUG9


#ifndef CHRG_LOG_VERBOSE			/* normally not defined */
#define CHRG_LOG_DEFAULT    CHRG_MSK_INTERRUPTS
#else
#define CHRG_LOG_DEFAULT    (CHRG_MSK_READINGS | CHRG_MSK_CALCS | CHRG_MSK_API_CALLS | CHRG_MSK_INTERRUPTS)
#endif


#ifndef CHRG_LOG_LOUD
#define CHRG_LOG_READINGS(fmt, args...)   LLOG_DEBUG0(fmt, ## args)
#define CHRG_LOG_CALCS(fmt, args...)	  LLOG_DEBUG1(fmt, ## args)
#define CHRG_LOG_API_CALLS(fmt, args...)  LLOG_DEBUG2(fmt, ## args)
#define CHRG_LOG_INTERRUPTS(fmt, args...) LLOG_DEBUG3(fmt, ## args)
#define CHRG_LOG_ENTRY_EXIT(fmt, args...) LLOG_DEBUG7(fmt, ## args)
#else
#define CHRG_LOG_READINGS(fmt, args...)	  printk("<1>charger: " fmt, ## args)
#define CHRG_LOG_CALCS(fmt, args...)	  printk("<1>charger: " fmt, ## args)
#define CHRG_LOG_API_CALLS(fmt, args...)  printk("<1>charger: " fmt, ## args)
#define CHRG_LOG_INTERRUPTS(fmt, args...) printk("<1>charger: " fmt, ## args)
#define CHRG_LOG_ENTRY_EXIT(fmt, args...) LLOG_DEBUG7(fmt, ## args)
#endif


/* Logging messages */

// error messages

#define CHRG_MSG_ERROR_INVALID_BP_VOLTAGE	"00", \
						"bp=%d, fn=%s ", \
						" invalid bp\n"

#define CHRG_MSG_ERROR_DISABLING_CHARGER	"01", \
						"err=%d ", \
						" set_charger: error disabling charger\n"

#define CHRG_MSG_ERROR_ENABLING_CHARGER		"02", \
						"err=%d ", \
						" set_charger: error enabling charger\n"

#define CHRG_MSG_ERROR_GREEN_LED_INIT		"03", \
						"e0=%d, e1=%d, e2=%d, e3=%d, " \
						"e4=%d, e5=%d, e6=%d, e7=%d ", \
						" green led init error\n"

#define CHRG_MSG_ERROR_GREEN_LED_SET_CURRENT	"04", \
						"err=%d ", \
						" green led set current error\n"


#define CHRG_MSG_ERROR_GREEN_LED_SET_DUTY_CYCLE	"05", \
						"err=%d ", \
						" green led set duty cycle error\n"

#define CHRG_MSG_ERROR_SET_LED			"06", \
						"ret_y=%d, ret_g=%d ", \
						" set_led: error\n"

#define CHRG_MSG_ERROR_CHRGRAW_READ		"07", \
						"pstat=%d ", \
						" CHRGRAW read failed\n"

#define CHRG_MSG_ERROR_BP_READ			"08", \
						"pstat=%d ", \
						" BP read failed\n"

#define CHRG_MSG_ERROR_BATT_READ		"09", \
						"pstat=%d ", \
						" BATT read failed\n"

#define CHRG_MSG_ERROR_CHRG_CURRENT_READ	"10", \
						"pstat=%d ", \
						" CHRG_CURRENT read failed\n"

#define CHRG_MSG_ERROR_BATT_CURRENT_READ	"11", \
						"pstat=%d ", \
						" BATT_CURRENT read failed\n"

#define CHRG_MSG_ERROR_INVALID_MILLIAMPS	"12", \
						"mA=%d ", \
						" lookup_ichrg: invalid mA\n"

#define CHRG_MSG_ERROR_POSTING_CRIT_BATT_EVT	"13", \
						"evt=critical ", \
						" Failed to post critical battery event\n"

#define CHRG_MSG_ERROR_POSTING_LOW_BATT_EVT	"14", \
						"evt=low ", \
						" Failed to post low battery event\n"

#define CHRG_MSG_ERROR_SUBSCRIBE_LOBATHI	"15", \
						"err=%d, intr=LOBATHI ", \
						" pmic_event_subscribe failed\n"

#define CHRG_MSG_ERROR_UNSUBSCRIBE_LOBATHI	"16", \
						"err=%d, intr=LOBATHI ", \
						" pmic_event_unsubscribe failed\n"

#define CHRG_MSG_ERROR_SUBSCRIBE_LOBATLI	"17", \
						"err=%d, intr=LOBATLI ", \
						" pmic_event_subscribe failed\n"

#define CHRG_MSG_ERROR_UNSUBSCRIBE_LOBATLI	"18", \
						"err=%d, intr=LOBATLI ", \
						" pmic_event_unsubscribe failed\n"

#define CHRG_MSG_ERROR_CREATING_WORKQUEUE	"19", \
						"", \
						" Failed to create \"charger\" workqueue\n"

#define CHRG_MSG_ERROR_REGISTERING		"20", \
						"err=%d ", \
						" %s failed\n"

// warning messages

#define CHRG_MSG_WARN_BP_ABOVE_CHRGRAW		"100", \
						"bp=%d, chrgraw=%d ", \
						" bp > chrgraw\n"

#define CHRG_MSG_WARN_BP_VERY_LOW		"101", \
						"bp=%d ", \
						" bp is surprisingly low\n"

#define CHRG_MSG_WARN_CHRGRAW_VERY_HIGH		"102", \
						"chrgraw=%d ", \
						" chrgraw is surprisingly high\n"

#define CHRG_MSG_WARN_UNEXPECTED_READING	"103", \
						"chrgraw=%d, bp=%d, batt=%d ", \
						" Unexpected reading\n"

#define CHRG_MSG_WARN_SPURIOUS_INTERRUPT	"104", \
						"LOBATHI=%d, LOBATLI=%d ", \
						" Spurious low-battery interrupt(s)\n"


// info messages

#define CHRG_MSG_INFO_LOW_BATT_READINGS		"200", \
						"bp=%d, batt=%d ", \
						" Low-battery readings\n"

#define CHRG_MSG_INFO_POSTED_CRIT_BATT_EVT	"201", \
						"evt=critical ", \
						" Posted critical battery event\n"

#define CHRG_MSG_INFO_POSTED_LOW_BATT_EVT	"202", \
						"evt=low ", \
						" Posted low battery event\n"

#define CHRG_MSG_INFO_CHARGE_LED_CHANGE		"203", \
						"from=%s, to=%s ", \
						" Charge LED changing from \'%s\' to \'%s\'\n"

#define CHRG_MSG_INFO_DRIVER_LOADING		"204", \
						"", \
						" Battery charger driver loading\n"

// debug messages (which are simpler than higher-priority messages)
//
// LLOG_DEBUGn macro supplies the following portion of the message:
//
//   "<date>:<time> charger: D def:" <your_fmt_string_here> " (<func>:<file>:<line>)" [missing \n ???]

// CHRG_LOG_READINGS-related messages

#define CHRG_MSG_DBG_VOLTAGE_READINGS		"300" \
						" chrgraw=%d, bp=%d, batt=%d :" \
						" Voltage readings (%d/%d/%d raw)\n"

#define CHRG_MSG_DBG_CURRENT_READINGS		"301" \
						" chrg_current=%d, batt_current=%d :" \
						" Current readings (%d/%d raw)\n"

// CHRG_LOG_CALCS-related messages

#define CHRG_MSG_DBG_MAX_CURRENT_LIMITED	"310" \
						" max_current=%d :" \
						" Limited due to surprises\n"

#define CHRG_MSG_DBG_THEORETICAL_CURRENT	"311" \
						" power_max=%d, delta_v=%d," \
						" theoretical_current=%d :" \
						" delta_v = (%d - %d)\n"

#define CHRG_MSG_DBG_TABLE_CURRENT_LIMIT	"312" \
						" table_current_limit=%d :" \
						" table-based current limit\n"

#define CHRG_MSG_DBG_MAX_CURRENT		"314" \
						" max_current=%d :" \
						" maximum current allowed\n"

#define CHRG_MSG_DBG_ICHRG_CALCS		"315" \
						" mA=%d, i=%d, ichrg_table[%d]=%d :" \
						" lookup_ichrg results\n"

#define CHRG_MSG_DBG_TEMP_AND_VBUS		"316" \
						" temp_ok=%d, vbus_present=%d :" \
						" Regulator should be off\n"

// CHRG_LOG_API_CALLS-related messages

#define CHRG_MSG_DBG_SET_CURRENT_LIMIT		"320" \
						" mA=%d :" \
						" Request to set current limit\n"

#define CHRG_MSG_DBG_HANDLE_CHARGING		"321" \
						" :" \
						" USB driver transferring current limit control\n"

#define CHRG_MSG_DBG_SET_CALLBACK		"322" \
						" callback=0x%p, arg=0x%p :" \
						" USB driver registering callback\n"

// CHRG_LOG_INTERRUPTS-related messages

#define CHRG_MSG_DBG_INTERRUPT_CTRL		"330 : %s\n"

#define CHRG_MSG_DBG_INTERRUPT			"331 : %s\n"

// CHRG_LOG_ENTRY_EXIT-related messages

#define CHRG_MSG_DBG_ENTRY			"340 : >>> %s\n"

#define CHRG_MSG_DBG_ENTRY_ARGS			"341 : >>> %s"

#define CHRG_MSG_DBG_EXIT			"342 : <<< %s\n"

/* Logging macros */

#define log_chrg_err(msg_id__args_fmt__msg_fmt, args...) \
		LLOG_ERROR(msg_id__args_fmt__msg_fmt, ## args)

#define log_chrg_warn(msg_id__args_fmt__msg_fmt, args...) \
		LLOG_WARN(msg_id__args_fmt__msg_fmt, ## args)

#define log_chrg_info(msg_id__args_fmt__msg_fmt, args...) \
		LLOG_INFO(msg_id__args_fmt__msg_fmt, ## args)



#define log_chrg_readings(msg_id__args_fmt__msg_fmt, args...) \
		if (LLOG_G_LOG_MASK & CHRG_MSK_READINGS) { \
			CHRG_LOG_READINGS(msg_id__args_fmt__msg_fmt, ## args); \
		}

#define log_chrg_calcs(msg_id__args_fmt__msg_fmt, args...) \
		if (LLOG_G_LOG_MASK & CHRG_MSK_CALCS) { \
			CHRG_LOG_CALCS(msg_id__args_fmt__msg_fmt, ## args); \
		}

#define log_chrg_api_calls(msg_id__args_fmt__msg_fmt, args...) \
		if (LLOG_G_LOG_MASK & CHRG_MSK_API_CALLS) { \
			CHRG_LOG_API_CALLS(msg_id__args_fmt__msg_fmt, ## args); \
		}

#define log_chrg_interrupts(msg_id__args_fmt__msg_fmt, args...) \
		if (LLOG_G_LOG_MASK & CHRG_MSK_INTERRUPTS) { \
			CHRG_LOG_INTERRUPTS(msg_id__args_fmt__msg_fmt, ## args); \
		}


#define log_chrg_entry() \
		if (LLOG_G_LOG_MASK & CHRG_MSK_ENTRY_EXIT) { \
			CHRG_LOG_ENTRY_EXIT(CHRG_MSG_DBG_ENTRY, __FUNCTION__); \
		}

#define log_chrg_entry_args(fmt, args...) \
		if (LLOG_G_LOG_MASK & CHRG_MSK_ENTRY_EXIT) { \
			CHRG_LOG_ENTRY_EXIT(CHRG_MSG_DBG_ENTRY_ARGS fmt "\n", \
					__FUNCTION__, ## args); \
		}

#define log_chrg_exit() \
		if (LLOG_G_LOG_MASK & CHRG_MSK_ENTRY_EXIT) { \
			CHRG_LOG_ENTRY_EXIT(CHRG_MSG_DBG_EXIT, __FUNCTION__); \
		}

