/*
 * battery.c - TI BQ27010 I2C client
 *
 * Copyright (C) 2009 Amazon Technologies, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <asm/arch/board_id.h>

#include "battery.h"

#define DRIVER_NAME "battery"
#define DRIVER_VERSION "version=1.0"

#define LLOG_G_LOG_MASK battery_llog_mask
#define LLOG_KERNEL_COMP DRIVER_NAME
#include <llog.h>

#define log_err(msg_id_str, args_fmt, msg_fmt, args...)  \
		LLOG_ERROR(msg_id_str, args_fmt, msg_fmt, ##args)
#define log_info(msg_id_str, args_fmt, msg_fmt, args...) \
		LLOG_INFO(msg_id_str, args_fmt, msg_fmt, ##args)

/* Delay in seconds between gas gauge checks */
#define BATTERY_CHECK_DELAY	7
#define BATTERY_CHECK_DELAY_MAX	15

/* Iterations between gas gauge checks */
#define BATTERY_CHECK_ID_COUNT	31

#define BATTERY_LOW_TEMP_THRESHOLD	37	/* degrees F */
#define BATTERY_HIGH_TEMP_THRESHOLD	108

#define BATTERY_LOW_VOLT_THRESHOLD	2500	/* mV */
#define BATTERY_HIGH_VOLT_THRESHOLD	4250

#define BATTERY_ID_MASK		0x3F
#define BATTERY_ID_VALID_VALUE	0x00

#define BATTERY_I2C_ADDRESS	0x55

#define BATTERY_UNKNOWN_VALUE	 -4096	/* Unknown voltage, temp, etc. */

#define BATTERY_COMMS_ERR	-1
#define BATTERY_RANGE_ERR	-2

static int battery_attach_adapter(struct i2c_adapter *adapter);
static int battery_detach_client(struct i2c_client *client);
static void battery_device_release (struct device *dev);
static int battery_suspend (struct platform_device *pdev, pm_message_t state);
static int battery_resume (struct platform_device *pdev);
static int battery_probe (struct platform_device *pdev);
static int battery_remove (struct platform_device *pdev);


unsigned int battery_llog_mask;

static int battery_monitoring_disable = 0;
static int battery_compliance_required = 0;

static int (*battery_callback)(void *, int) = NULL;
static void *battery_callback_arg = NULL;

int battery_error_flags = BATTERY_INVALID_ID | \
				 BATTERY_COMMS_FAILURE | \
				 BATTERY_TEMP_OUT_OF_RANGE | \
				 BATTERY_VOLTAGE_OUT_OF_RANGE;
EXPORT_SYMBOL(battery_error_flags);

static int battery_check_delay = BATTERY_CHECK_DELAY;
static int battery_counter = 0;

static int battery_temp_low_threshold = BATTERY_LOW_TEMP_THRESHOLD;
static int battery_temp_high_threshold = BATTERY_HIGH_TEMP_THRESHOLD;

static int battery_voltage_low_threshold = BATTERY_LOW_VOLT_THRESHOLD;
static int battery_voltage_high_threshold = BATTERY_HIGH_VOLT_THRESHOLD;

static int battery_last_id = BATTERY_UNKNOWN_VALUE;
static int battery_last_temp = BATTERY_UNKNOWN_VALUE;
static int battery_last_voltage = BATTERY_UNKNOWN_VALUE;

static struct delayed_work battery_work;

static struct platform_device battery_dev = {
	.name = DRIVER_NAME,
	.id   = -1,
	.dev  = {
		 .release = battery_device_release,
		},
};

static struct platform_driver battery_drvr = {
	.driver  = {
			.name = DRIVER_NAME,
			.bus  = &platform_bus_type,
		   },
	.suspend = battery_suspend,
	.resume  = battery_resume,
	.probe   = battery_probe,
	.remove  = battery_remove,
};

static struct i2c_driver battery_i2c_driver = {
	.driver = {
			.name  = DRIVER_NAME,
		  },
	.attach_adapter = battery_attach_adapter,
	.detach_client  = battery_detach_client,
};

static struct i2c_client battery_i2c_client = {
	.name = DRIVER_NAME,
	.addr = BATTERY_I2C_ADDRESS,
	.driver = &battery_i2c_driver,
};

static unsigned short normal_i2c[] = { BATTERY_I2C_ADDRESS, I2C_CLIENT_END };

I2C_CLIENT_INSMOD_1(battery);

void battery_register_callback(int (*callback)(void *, int), void *arg)
{
	log_info("rgcb", "cb=0x%p,arg=0x%p", "battery_register_callback\n",
			callback, arg);
	if (callback) {
		battery_callback = callback;
		battery_callback_arg = arg;
	} else {
		battery_callback = NULL;
		battery_callback_arg = NULL;
	}
}
EXPORT_SYMBOL(battery_register_callback);

int battery_read_reg(unsigned char reg_num, unsigned char *value)
{
	s32	retval;

	if (!battery_i2c_client.adapter) {
		log_info("nrdy", "", "Not ready\n");
		return -EAGAIN;
	}

	retval = i2c_smbus_read_byte_data(&battery_i2c_client, reg_num);
	if (retval < 0) {
		log_info("rd", "err=%d", "i2c_smbus_read_byte_data(0x%02X) "
				"failed\n",
				retval, reg_num);
		return -EIO;
	}

	*value = (unsigned char) (retval & 0xFF);
	return 0;
}
EXPORT_SYMBOL(battery_read_reg);

static int battery_read_one_reg(unsigned char reg_num, unsigned char *value)
{
	int	err;
	int	old_comms_bad = battery_error_flags & BATTERY_COMMS_FAILURE;

	err = battery_read_reg(reg_num, value);

	if (err) {
		battery_error_flags |= BATTERY_COMMS_FAILURE;
		if (!old_comms_bad)
			log_info("rdbd", "err=%d", "Comms failure\n", err);
	} else {
		battery_error_flags &= ~BATTERY_COMMS_FAILURE;
		if (old_comms_bad)
			log_info("rdgd", "", "Comms working\n");
	}
	return err;
}

static int battery_read_id(int *id)
{
	unsigned char	value = 0xFF;
	int		err;
	int		old_id_bad = battery_error_flags & BATTERY_INVALID_ID;

	err = battery_read_one_reg(BATTERY_ID, &value);
	if (err) {
		*id = BATTERY_UNKNOWN_VALUE;
		battery_error_flags |= BATTERY_INVALID_ID;
		if (!old_id_bad)
			log_info("noid", "", "ID unknown\n");
		return BATTERY_COMMS_ERR;
	}

	*id = value;
	if (battery_compliance_required) {
		if ((value & BATTERY_ID_MASK) != BATTERY_ID_VALID_VALUE) {
			battery_error_flags |= BATTERY_INVALID_ID;
			if (!old_id_bad)
				log_info("bdid","id=0x%02X","Bad battery ID\n",
						value);
			return BATTERY_RANGE_ERR;
		}
		/* valid ID - fall through */
	}
	/* If we're here, either we don't care what the ID is, or we do care
	 * and it is valid. */
	battery_error_flags &= ~BATTERY_INVALID_ID;
	if (old_id_bad)
		log_info("idgd", "", "Good battery ID\n");
	return 0;
}

static int battery_read_temperature(int *temperature)
{
	unsigned char	hi, lo;
	int		celsius, fahrenheit;
	int		errhi, errlo;
	int		old_temp_bad = battery_error_flags | \
					BATTERY_TEMP_OUT_OF_RANGE;

	errlo = battery_read_one_reg(BATTERY_TEMPL, &lo);
	errhi = battery_read_one_reg(BATTERY_TEMPH, &hi);

	if (errlo || errhi) {
		*temperature = BATTERY_UNKNOWN_VALUE;
		battery_error_flags |= BATTERY_TEMP_OUT_OF_RANGE;
		if (!old_temp_bad)
			log_info("notp", "", "Temperature unknown\n");
		return BATTERY_COMMS_ERR;
	}

	/* Units in 0.25 degrees Kelvin - divide by 4, round, and subtract
	 * 273 to get Celsius.  Multiply by 1.8 and add 32 for Fahrenheit. */
	celsius = ((((hi << 8) | lo) + 2) / 4) - 273;
	fahrenheit = ((celsius * 9) / 5) + 32;

	*temperature = fahrenheit;
	if ((battery_temp_low_threshold <= fahrenheit) &&
	    (fahrenheit <= battery_temp_high_threshold)) {
		battery_error_flags &= ~BATTERY_TEMP_OUT_OF_RANGE;
		return 0;
	} else {
		battery_error_flags |= BATTERY_TEMP_OUT_OF_RANGE;
		return BATTERY_RANGE_ERR;
	}
}

static int battery_read_voltage(int *voltage)
{
	unsigned char	hi, lo;
	int		volts;
	int		errhi, errlo;
	int		old_voltage_bad = battery_error_flags | \
					BATTERY_VOLTAGE_OUT_OF_RANGE;

	errlo = battery_read_one_reg(BATTERY_VOLTL, &lo);
	errhi = battery_read_one_reg(BATTERY_VOLTH, &hi);

	if (errlo || errhi) {
		*voltage = BATTERY_UNKNOWN_VALUE;
		battery_error_flags |= BATTERY_VOLTAGE_OUT_OF_RANGE;
		if (!old_voltage_bad)
			log_info("nov", "", "Voltage unknown\n");
		return BATTERY_COMMS_ERR;
	}

	/* Units in millivolts */
	volts = (hi << 8) | lo;

	*voltage = volts;
	if ((battery_voltage_low_threshold <= volts) &&
	    (volts <= battery_voltage_high_threshold)) {
		battery_error_flags &= ~BATTERY_VOLTAGE_OUT_OF_RANGE;
		return 0;
	} else {
		battery_error_flags |= BATTERY_VOLTAGE_OUT_OF_RANGE;
		return BATTERY_RANGE_ERR;
	}
}

static void battery_fake_good_readings(void)
{
	battery_error_flags = 0;
	battery_last_id = BATTERY_ID_VALID_VALUE;
	battery_last_temp = (BATTERY_LOW_TEMP_THRESHOLD + \
			     BATTERY_HIGH_TEMP_THRESHOLD)/2;
	battery_last_voltage = (BATTERY_LOW_VOLT_THRESHOLD + \
				BATTERY_HIGH_VOLT_THRESHOLD)/2;
}

static void battery_work_fn(struct work_struct *work)
{
	int err0 = 0, err1 = 0, err2 = 0;
	int old_error_flags = battery_error_flags;
	int old_id_error = old_error_flags & BATTERY_INVALID_ID;

	if (battery_monitoring_disable) {
		/* Setup fake readings that are acceptable, and then return
		 * without scheduling battery_work again.  It will get
		 * scheduled again when battery monitoring is re-enabled.
		 */
		battery_fake_good_readings();
		return;
	}

	if ((battery_counter == 0) || old_id_error)
		err0 = battery_read_id(&battery_last_id);
	
	if (battery_counter == 0)
		battery_counter = BATTERY_CHECK_ID_COUNT;
	else
		battery_counter--;

	err1 = battery_read_temperature(&battery_last_temp);
	err2 = battery_read_voltage(&battery_last_voltage);

	if (err0 || err1 || err2) {
		log_info("rdgs","err0=%d,err1=%d,err2=%d",
				"Battery readings failed\n",
				err0, err1, err2);
	}

	/* Log if battery_error_flags has changed.  In addition, if
	 * battery_error_flags remain set for a long period of time, log
	 * a message periodically as well.  (Once every 32 passes, or so.)
	 */
	if ((battery_error_flags != old_error_flags) ||
	    (battery_error_flags && (battery_counter == 1))) {
		/* Log that some change occurred */
		int flags = battery_error_flags;
		int id_err    = ((flags & BATTERY_INVALID_ID) != 0);
		int comms_err = ((flags & BATTERY_COMMS_FAILURE) != 0);
		int temp_err  = ((flags & BATTERY_TEMP_OUT_OF_RANGE) != 0);
		int volt_err  = ((flags & BATTERY_VOLTAGE_OUT_OF_RANGE) != 0);
		if (battery_error_flags) {
			log_err("flge", "id=%d,comms=%d,temp=%d,voltage=%d",
					"Battery error flags changed\n",
					id_err, comms_err, temp_err, volt_err);
		} else {
			log_info("flgi", "", "Battery error flags cleared\n");
		}
		/* If we transitioned either from no errors or to no errors,
		 * notify the charger driver of the change.
		 */
		if ((battery_error_flags == 0) || (old_error_flags == 0)) {
			log_info("clbk", "oef=%d,bef=%d,callback=0x%p",
					"Notification\n", old_error_flags,
					battery_error_flags, battery_callback);
			if (battery_callback)
				(*battery_callback)(battery_callback_arg,
						battery_error_flags);
		}
	}

	if (!battery_monitoring_disable)
		schedule_delayed_work(&battery_work,battery_check_delay * HZ);
}

static int battery_detect(struct i2c_adapter *adapter, int address, int kind)
{
	log_info("detect", "", "battery detected\n");
	battery_i2c_client.adapter = adapter;
	return 0;
}

static int battery_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, battery_detect);
}

static int battery_detach_client(struct i2c_client *client)
{
	return i2c_detach_client(client);
}

/* sysfs entries */

/* "flags" sysfs attribute - read only */

static ssize_t
show_flags (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_error_flags);
}

static DEVICE_ATTR (flags, S_IRUGO, show_flags, NULL);

/* "delay" sysfs attribute - read/write */

static ssize_t
show_delay (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_check_delay);
}

static ssize_t
store_delay (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((0 < value) &&
			 (value <= BATTERY_CHECK_DELAY_MAX))) {
		log_info("dly", "value=%d", "Setting battery_check_delay\n",
				value);
		battery_check_delay = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (delay, S_IRUGO|S_IWUSR, show_delay, store_delay);

/* "disable" sysfs attribute - read/write */

static ssize_t
show_disable (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_monitoring_disable);
}

static ssize_t
store_disable (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value = -1;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((value == 0) || (value == 1))) {
		battery_monitoring_disable = value;
		if (battery_monitoring_disable) {
			/* Stop the battery monitoring workqueue element,
			 * and report acceptable monitored values.
			 * Inform the charger driver, if necessary.
			 */
			log_info("bmd", "battery_monitoring_disable=%d",
				"Disabling battery monitoring\n", value);
			cancel_rearming_delayed_work(&battery_work);
			battery_fake_good_readings();
			if (battery_callback)
				(*battery_callback)(battery_callback_arg,
						battery_error_flags);
		} else {
			/* Restart battery monitoring.  If there are any
			 * issues, the charger driver will be informed
			 * the next time battery_work is run.
			 */
			log_info("bme", "battery_monitoring_disable=%d",
				"Enabling battery monitoring\n", value);
			schedule_delayed_work(&battery_work, HZ);
		}
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (disable, S_IRUGO|S_IWUSR, show_disable, store_disable);

/* "id" sysfs attribute - read only */

static ssize_t
show_id (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_last_id);
}

static DEVICE_ATTR (id, S_IRUGO, show_id, NULL);

/* "temp" sysfs attribute - read only */

static ssize_t
show_temp (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_last_temp);
}
static DEVICE_ATTR (temp, S_IRUGO, show_temp, NULL);

/* "thi" sysfs attribute - read/write */

static ssize_t
show_thi (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_temp_high_threshold);
}

static ssize_t
store_thi (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((battery_temp_low_threshold < value) &&
			 (value <= BATTERY_HIGH_TEMP_THRESHOLD))) {
		log_info("thi", "value=%d", "Setting "
				"battery_temp_high_threshold\n", value);
		battery_temp_high_threshold = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (thi, S_IRUGO|S_IWUSR, show_thi, store_thi);

/* "tlo" sysfs attribute - read/write */

static ssize_t
show_tlo (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_temp_low_threshold);
}

static ssize_t
store_tlo (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((BATTERY_LOW_TEMP_THRESHOLD <= value) &&
			 (value < battery_temp_high_threshold))) {
		log_info("tlo", "value=%d", "Setting "
				"battery_temp_low_threshold\n", value);
		battery_temp_low_threshold = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (tlo, S_IRUGO|S_IWUSR, show_tlo, store_tlo);

/* "volt" sysfs attribute - read only */

static ssize_t
show_volt (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_last_voltage);
}
static DEVICE_ATTR (volt, S_IRUGO, show_volt, NULL);

/* "vhi" sysfs attribute - read/write */

static ssize_t
show_vhi (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf,PAGE_SIZE,"%d\n",battery_voltage_high_threshold);
}

static ssize_t
store_vhi (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((battery_voltage_low_threshold < value) &&
			 (value <= BATTERY_HIGH_VOLT_THRESHOLD))) {
		log_info("vhi", "value=%d", "Setting "
				"battery_voltage_high_threshold\n", value);
		battery_voltage_high_threshold = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (vhi, S_IRUGO|S_IWUSR, show_vhi, store_vhi);

/* "vlo" sysfs attribute - read/write */

static ssize_t
show_vlo (struct device *_dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf,PAGE_SIZE,"%d\n",battery_voltage_low_threshold);
}

static ssize_t
store_vlo (struct device *_dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int value;

	if ((sscanf(buf, "%d", &value) > 0) &&
			((BATTERY_LOW_VOLT_THRESHOLD <= value) &&
			 (value < battery_voltage_high_threshold))) {
		log_info("vlo", "value=%d", "Setting "
				"battery_voltage_low_threshold\n", value);
		battery_voltage_low_threshold = value;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR (vlo, S_IRUGO|S_IWUSR, show_vlo, store_vlo);


/* Platform driver support */

static int battery_suspend (struct platform_device *pdev, pm_message_t state)
{
	log_info("susp", "state.event=%d", "battery_suspend\n", state.event);
	cancel_rearming_delayed_work(&battery_work);
	return 0;
}

static int battery_resume (struct platform_device *pdev)
{
	log_info("resm", "", "battery_resume\n");
	battery_counter = 0;	/* check ID and temperature */
	schedule_delayed_work(&battery_work, 1);
	return 0;
}

static int battery_probe (struct platform_device *pdev)
{
	log_info("prob", "", "battery_probe\n");
	return 0;
}

static int battery_remove (struct platform_device *pdev)
{
	log_info("remv", "", "battery_remove\n");
	return 0;
}

static void battery_device_release (struct device *dev)
{
	log_info("dvrl", "", "battery_device_release\n");
}

static int __init battery_init(void)
{
	int err;

	LLOG_INIT();

	log_info("init", "%s", "Init\n", DRIVER_VERSION);

	err = i2c_add_driver(&battery_i2c_driver);
	if (err) {
		log_err("ii2c", "err=%d", "i2c_add_driver failed\n", err);
		err = -1;
		goto exit1;
	}

	err = platform_device_register(&battery_dev);
	if (err) {
		log_err("idev", "err=%d", "platform_device_register failed\n",
				err);
		err = -2;
		goto exit2;
	}

	err = platform_driver_register(&battery_drvr);
	if (err) {
		log_err("idrv", "err=%d", "platform_driver_register failed\n",
				err);
		err = -3;
		goto exit3;
	}
	
	err = device_create_file(&battery_dev.dev, &dev_attr_flags);
	err = device_create_file(&battery_dev.dev, &dev_attr_delay);
	err = device_create_file(&battery_dev.dev, &dev_attr_disable);
	err = device_create_file(&battery_dev.dev, &dev_attr_id);
	err = device_create_file(&battery_dev.dev, &dev_attr_temp);
	err = device_create_file(&battery_dev.dev, &dev_attr_thi);
	err = device_create_file(&battery_dev.dev, &dev_attr_tlo);
	err = device_create_file(&battery_dev.dev, &dev_attr_volt);
	err = device_create_file(&battery_dev.dev, &dev_attr_vhi);
	err = device_create_file(&battery_dev.dev, &dev_attr_vlo);

	if (IS_TURINGWW()) {
		battery_compliance_required = 1;
		log_info("cmpl", "", "Battery compliance required\n");
	}

	if (IS_MARIO() || IS_ADS()) {
		battery_monitoring_disable = 1;
		battery_fake_good_readings();
		log_info("nobt", "", "No battery expected - no monitoring\n");
	}

	INIT_DELAYED_WORK(&battery_work, battery_work_fn);
	schedule_delayed_work(&battery_work, 1);
	return 0;

exit3:
	platform_device_unregister(&battery_dev);
exit2:
	i2c_del_driver(&battery_i2c_driver);
exit1:
	log_err("ierr", "err=%d", "battery_init failed\n", err);
	return err;
}

static void __exit battery_exit(void)
{
	cancel_rearming_delayed_work(&battery_work);
	i2c_del_driver(&battery_i2c_driver);
	device_remove_file(&battery_dev.dev, &dev_attr_flags);
	device_remove_file(&battery_dev.dev, &dev_attr_delay);
	device_remove_file(&battery_dev.dev, &dev_attr_disable);
	device_remove_file(&battery_dev.dev, &dev_attr_id);
	device_remove_file(&battery_dev.dev, &dev_attr_temp);
	device_remove_file(&battery_dev.dev, &dev_attr_thi);
	device_remove_file(&battery_dev.dev, &dev_attr_tlo);
	device_remove_file(&battery_dev.dev, &dev_attr_volt);
	device_remove_file(&battery_dev.dev, &dev_attr_vhi);
	device_remove_file(&battery_dev.dev, &dev_attr_vlo);
	platform_driver_unregister(&battery_drvr);
	platform_device_unregister(&battery_dev);
}

MODULE_AUTHOR("Lab126");
MODULE_DESCRIPTION("TI BQ27010 battery gas gauge driver");
MODULE_LICENSE("GPL");

module_init(battery_init);
module_exit(battery_exit);

