/*
 * mwan.c  --  Mario WAN hardware control driver
 *
 * Copyright 2005-2009 Lab126, Inc.  All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/hardware.h>
#include <asm/uaccess.h>
#include <net/mwan.h>
#include <asm/arch/board_id.h>


extern void gpio_wan_init(void *callback);
extern void gpio_wan_exit(void *callback);
extern void gpio_wan_power(int enable);
extern void gpio_wan_usbhc1_pwr(int enable);


#undef DEBUG

#ifdef DEBUG
#define log_debug(format, arg...) printk("mwan: D %s:" format, __func__, ## arg)
#else
#define log_debug(format, arg...)
#endif

#define log_info(format, arg...) printk("mwan: I %s:" format, __func__, ## arg)
#define log_err(format, arg...) printk("mwan: E %s:" format, __func__, ## arg)

#define VERSION			"0.8.5"

#define PROC_WAN		"wan"
#define PROC_WAN_POWER		"power"
#define PROC_WAN_ENABLE		"enable"

static struct proc_dir_entry *proc_wan_parent;
static struct proc_dir_entry *proc_wan_power;
static struct proc_dir_entry *proc_wan_enable;

static wan_status_t wan_status = WAN_OFF;
static int wan_enable = 0;


#define WAN_STRING_CLASS	"wan"
#define WAN_STRING_DEV		"mwan"

static struct file_operations mwan_ops = {
	.owner = THIS_MODULE,
	.ioctl = NULL,		// (add ioctl support, if desired)
};


static struct class *wan_class = NULL;
static struct class_device *wan_class_dev = NULL;
static int wan_major = 0;

static unsigned long tph_last_seconds = 0;

// standard network deregistration time definition:
//   2s -- maximum time required between the start of power down and that of IMSI detach
//   5s -- maximum time required for the IMSI detach
//   5s -- maximum time required between the IMSI detach and power down finish (time
//         required to stop tasks, etc.)
//   3s -- recommended safety margin
#define NETWORK_DEREG_TIME	((12 + 3) * 1000)

// minimum allowed delay between TPH notifications
#define WAKE_EVENT_INTERVAL	10


static void
set_wan_rf_enable(
	int enable)
{
	extern void gpio_wan_rf_enable(int);

	if (enable != wan_enable) {
		log_debug("swe:enable=%d:setting WAN RF enable state\n", enable);

		gpio_wan_rf_enable(enable);

		wan_enable = enable;
	}
}


static inline int
get_wan_rf_enable(
	void)
{
	return wan_enable;
}


static int
set_wan_power(
	wan_status_t new_status)
{
	wan_status_t check_status = new_status == WAN_OFF_KILL ? WAN_OFF : new_status;

	if (check_status == wan_status) {
		return 0;
	}

	// ignore any spurious WAKE line events during module power processing
	tph_last_seconds = get_seconds();

	switch (new_status) {

		case WAN_ON :
			log_debug("pow:status=%d:powering on WAN module\n", new_status);

			// power up the WAN
			gpio_wan_power(1);

			// enable the WAN RF subsystem
			set_wan_rf_enable(1);
			break;

		default :
			log_err("req_err:request=%d:unknown power request\n", new_status);

			// (fall through)

		case WAN_OFF :
		case WAN_OFF_KILL :
			// disable the WAN RF subsystem
			set_wan_rf_enable(0);

			if (new_status != WAN_OFF_KILL) {
				// wait the necessary deregistration interval
				msleep(NETWORK_DEREG_TIME);
			}

			log_debug("pow:status=%d:powering off WAN module\n", new_status);

			// power off the WAN
			gpio_wan_power(0);

			new_status = WAN_OFF;
			break;

	}

	wan_status = new_status;

	wan_set_power_status(wan_status);

	return 0;
}


static int
proc_power_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", wan_status == WAN_ON ? 1 : 0);
}


static int
proc_power_write(
        struct file *file,
        const char __user *buf,
        unsigned long count,
        void *data)
{
	char lbuf[16];
	wan_status_t new_wan_status, prev_wan_status = wan_status;

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) {
		return -EFAULT;
	}

	if (lbuf[0] == '2') {
		// turn off, skipping deregistration
		set_wan_power(WAN_OFF_KILL);

	} else {
		// perform normal on/off power handling
		new_wan_status = lbuf[0] == '0' ? WAN_OFF : WAN_ON;

		if ((new_wan_status == WAN_ON && prev_wan_status == WAN_OFF) ||
		    (new_wan_status == WAN_OFF && prev_wan_status == WAN_ON)) {
			set_wan_power(new_wan_status);
		}
	}

	return count;
}


static int
proc_enable_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	*eof = 1;

	return sprintf(page, "%d\n", get_wan_rf_enable());
}


static int
proc_enable_write(
        struct file *file,
        const char __user *buf,
        unsigned long count,
        void *data)
{
	char lbuf[16];

	memset(lbuf, 0, sizeof(lbuf));

	if (copy_from_user(lbuf, buf, 1)) {
		return -EFAULT;
	}

	set_wan_rf_enable(lbuf[0] != '0');

	return count;
}


static void
wan_tph_notify(
	void)
{
	kobject_uevent(&wan_class_dev->kobj, KOBJ_CHANGE);
}


static void
wan_tph_event_handler(
	void)
{
	unsigned long tph_cur_seconds, tph_delta;

	// the module may generate extraneous TPH events; filter out these extra events
	tph_cur_seconds = get_seconds();

	tph_delta = (tph_last_seconds <= tph_cur_seconds) ?
			(tph_cur_seconds - tph_last_seconds) : 
			(WAKE_EVENT_INTERVAL + 1);

	if (tph_delta > WAKE_EVENT_INTERVAL) {
		tph_last_seconds = tph_cur_seconds;

		log_info("tph::tph event occurred; notifying system of TPH\n");

		wan_tph_notify();
	}
}


static int
wan_init(
	void)
{
	int ret;

	gpio_wan_init(wan_tph_event_handler);

	wan_major = register_chrdev(0, WAN_STRING_DEV, &mwan_ops);
	if (wan_major < 0) {
		ret = wan_major;
		log_err("dev_err:device=" WAN_STRING_DEV ",err=%d:could not register device\n", ret);
		goto exit1;
	}

	wan_class = class_create(THIS_MODULE, WAN_STRING_CLASS);
	if (IS_ERR(wan_class)) {
		ret = PTR_ERR(wan_class);
		log_err("class_err:err=%d:could not create class\n", ret);
		goto exit2;
	}

	wan_class_dev = class_device_create(wan_class, NULL, MKDEV(wan_major, 0), NULL, WAN_STRING_DEV);
	if (IS_ERR(wan_class_dev)) {
		ret = PTR_ERR(wan_class_dev);
		log_err("class_dev_err:err=%d:could not create class device\n", ret);
		goto exit3;
	}

	wan_set_power_status(WAN_OFF);

	ret = 0;
	goto exit0;

exit3:
	class_destroy(wan_class);
	wan_class = NULL;

exit2:
	unregister_chrdev(wan_major, WAN_STRING_DEV);

exit1:
	gpio_wan_exit(wan_tph_event_handler);

exit0:
	return ret;
}


static void
wan_exit(
	void)
{
	wan_set_power_status(WAN_INVALID);

	gpio_wan_exit(wan_tph_event_handler);

	if (wan_class_dev != NULL) {
		class_device_destroy(wan_class, MKDEV(wan_major, 0));
		wan_class_dev = NULL;
		class_destroy(wan_class);
		unregister_chrdev(wan_major, WAN_STRING_DEV);
	}
}


static int __init
mwan_init(
	void)
{
	int ret = 0;

        log_info("init:mario WAN hardware driver " VERSION "\n");

	// create the "/proc/wan" parent directory
	proc_wan_parent = create_proc_entry(PROC_WAN, S_IFDIR | S_IRUGO | S_IXUGO, NULL);
	if (proc_wan_parent) {

		// create the "/proc/wan/power" entry
		proc_wan_power = create_proc_entry(PROC_WAN_POWER, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_power) {
			proc_wan_power->data = NULL;
			proc_wan_power->read_proc = proc_power_read;
			proc_wan_power->write_proc = proc_power_write;
		}

		// create the "/proc/wan/enable" entry
		proc_wan_enable = create_proc_entry(PROC_WAN_ENABLE, S_IWUSR | S_IRUGO, proc_wan_parent);
		if (proc_wan_enable) {
			proc_wan_enable->data = NULL;
			proc_wan_enable->read_proc = proc_enable_read;
			proc_wan_enable->write_proc = proc_enable_write;
		}

	} else {
		ret = -1;

	}

	if (ret == 0) {
		wan_init();
	}

	return ret;
}


static void __exit
mwan_exit(
	void)
{
	if (proc_wan_parent != NULL) {
		remove_proc_entry(PROC_WAN_ENABLE, proc_wan_parent);
		remove_proc_entry(PROC_WAN_POWER, proc_wan_parent);
		remove_proc_entry(PROC_WAN, NULL);

		proc_wan_enable = proc_wan_power = proc_wan_parent = NULL;
        }

	wan_exit();
}


module_init(mwan_init);
module_exit(mwan_exit);

MODULE_DESCRIPTION("Mario WAN hardware driver");
MODULE_AUTHOR("Lab126");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);


