/*
 * Amazon Kindle Power Button
 * Copyright (C) 2008 Manish Lachwani <lachwani@lab126.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/div64.h>
#include <asm/atomic.h>

/* There are 3 buttons on the Atlas PMIC and we use ONOFD1 */
#define MARIO_BUTTON_MINOR	158 /* Major 10, Minor 158, /dev/mariobutton */

/*
 * Uses Atlas PMIC
 */
#include <asm/arch/pmic_external.h>
#include <asm/arch/pmic_power.h>

static int mb_evt[2] = {KOBJ_OFFLINE, KOBJ_ONLINE};
static char *mb_evt_d[2] = {"HELD", "PRESSED"};

static struct miscdevice button_misc_device = {
	MARIO_BUTTON_MINOR,
	"mariobutton",
	NULL,
};

atomic_t lab126_mario_perf_log;
EXPORT_SYMBOL(lab126_mario_perf_log);

static ssize_t
show_perf_log(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&lab126_mario_perf_log));
}

static ssize_t
store_perf_log(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;

	if (sscanf(buf, "%d", &i) != 1)
		return -EINVAL;

	atomic_set(&lab126_mario_perf_log, i != 0);
	return count;
}

static DEVICE_ATTR(perf_log, 0644, show_perf_log, store_perf_log);

/*
 * Interrupt triggered when button pressed
 */
static void mario_button_handler(void *param)
{
	unsigned int sense;
	unsigned int press_event = 0;
	int i;
	struct timeval tv;
	s64 timestamp;
	int pb_counter = 100;   /* Monitor battery cut */

	/*
	 * reset button - restart
	 */
	pmic_power_set_auto_reset_en(0);
	pmic_power_set_conf_button(BT_ON1B, 0, 2);	
	
	pmic_read_reg(REG_INTERRUPT_SENSE_1, &sense, (1 << 3));

	if (!(sense & (1 << 3))) {
		/* Delay of about 2sec */
		for (i = 0; i < 100; i++) {
			pmic_read_reg(REG_INTERRUPT_SENSE_1, &sense, (1 << 3));

			if (sense & (1 << 3)) {
				press_event = 1;
				break;
			}

			msleep(35);
		}
	} else {
		pmic_power_set_auto_reset_en(1);
		pmic_power_set_conf_button(BT_ON1B, 1, 2);
		press_event = 1;
	}

	if (unlikely(atomic_read(&lab126_mario_perf_log))) {
		do_gettimeofday(&tv);
		timestamp = timeval_to_ns(&tv);
		do_div(timestamp, NSEC_PER_MSEC);
		printk(KERN_DEBUG "mario_button: P def:pbp:id=powerButtonPressed,time=%lld,type=absolute:sending uevent to user space\n", timestamp);
	}

	if (kobject_uevent(&button_misc_device.this_device->kobj,
			   mb_evt[press_event]))
		printk(KERN_WARNING "mariobutton: can't send uevent\n");

	if (!press_event)
		do {
			msleep(50);
			if (--pb_counter == 0)
				printk(KERN_EMERG "bcut: C def:pcut:: pending battery cut\n");
			pmic_read_reg(REG_INTERRUPT_SENSE_1, &sense, (1 << 3));
		} while (!(sense & (1 << 3)));

	/* Atlas N1B interrupt line debouce is 30 ms */
	msleep(40);

	/* ignore release interrupts */
	pmic_write_reg(REG_INTERRUPT_STATUS_1, (1 << 3), (1 << 3));

	pmic_write_reg(REG_INTERRUPT_MASK_1, 0, (1 << 3));
}

static int __init mario_power_button_init(void)
{
	int ret = 0;

	printk (KERN_INFO "Mario Power Button Driver\n");

	atomic_set(&lab126_mario_perf_log, 0);

	if (misc_register (&button_misc_device)) {
		printk (KERN_WARNING "mariobutton: Couldn't register device 10, "
				"%d.\n", MARIO_BUTTON_MINOR);
		ret = -EBUSY;
		goto err1;
	}

	if (pmic_power_set_conf_button(BT_ON1B, 0, 2)) {
		printk(KERN_WARNING "mariobutton: can't configure debounce "
		       "time\n");
		ret = -EIO;
		goto err2;
	}

	if (device_create_file(button_misc_device.this_device, &dev_attr_perf_log)) {
		printk(KERN_WARNING "mariobutton: can't register sysfs entry\n");
		ret = -EIO;
		goto err2;
	}


	if (pmic_power_event_sub(PWR_IT_ONOFD1I, mario_button_handler)) {
		printk(KERN_WARNING "mariobutton: can't subscribe to IRQ\n");
		ret = -EIO;
		goto err3;
	}

	/* Success */
	return 0;

err3:
	device_remove_file(button_misc_device.this_device, &dev_attr_perf_log);
err2:
	misc_deregister(&button_misc_device);
err1:
	return ret;
}

static void __exit mario_power_button_exit(void)
{
	pmic_power_event_unsub(PWR_IT_ONOFD1I, mario_button_handler);
	device_remove_file(button_misc_device.this_device, &dev_attr_perf_log);
	misc_deregister (&button_misc_device);
}

MODULE_AUTHOR("Manish Lachwani");
MODULE_LICENSE("GPL");

module_init(mario_power_button_init);
module_exit(mario_power_button_exit);

