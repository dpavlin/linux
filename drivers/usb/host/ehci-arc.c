/*
 * drivers/usb/host/ehci-arc.c
 *
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008-2009 Amazon Technologies, Inc. All Rights Reserved.
 * Manish Lachwani (lachwani@lab126.com)
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @defgroup USB ARC OTG USB Driver
 */
/*!
 * @file ehci-arc.c
 * @brief platform related part of usb host driver.
 * @ingroup USB
 */

/*!
 * Include files
 */

/* Note: this file is #included by ehci-hcd.c */

#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/usb/otg.h>
#include <linux/usb/fsl_xcvr.h>

#include <asm/io.h>
#include <asm/arch/fsl_usb.h>
#include "ehci-fsl.h"

#undef dbg
#undef vdbg

#if 0
#define dbg	printk
#else
#define dbg(fmt, ...) do {} while (0)
#endif

#if 0
#define vdbg	dbg
#else
#define vdbg(fmt, ...) do {} while (0)
#endif

#define EHCI_IDLE_SUSPEND_THRESHOLD	20000	/* Restart the idle thread 20 secs after resume */
extern void gpio_wan_usb_enable(int);
extern void ehci_hcd_recalc_work(void);
extern void ehci_hcd_restart_idle(void);

int wakeup_value = 0;
extern int suspend_count;

static ssize_t wakeup_enable_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", wakeup_value);
}

static ssize_t wakeup_enable_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	if (strstr(buf, "1") != NULL) {
		wakeup_value = 1;
		/* Restart the workqueue */
		ehci_hcd_restart_idle();
	}
	else {
		ehci_hcd_recalc_work();
		wakeup_value = 0;
	}
	
	return size;
}
static DEVICE_ATTR(wakeup_enable, 0644, wakeup_enable_show, wakeup_enable_store);


static ssize_t suspend_counter_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", suspend_count);
}

static DEVICE_ATTR(suspend_counter, 0644, suspend_counter_show, NULL);

extern void fsl_platform_set_vbus_power(struct fsl_usb2_platform_data *pdata,
					int on);

/* PCI-based HCs are common, but plenty of non-PCI HCs are used too */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_fsl_probe - initialize FSL-based HCDs
 * @drvier: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
static int usb_hcd_fsl_probe(const struct hc_driver *driver,
			     struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;
	static struct device *ehci_arc_dev;
	struct ehci_hcd *ehci;

	pr_debug("initializing FSL-SOC USB Controller\n");

	/* Need platform data for setup */
	if (!pdata) {
		dev_err(&pdev->dev,
			"No platform data for %s.\n", pdev->dev.bus_id);
		return -ENODEV;
	}

	retval = fsl_platform_verify(pdev);
	if (retval)
		return retval;

	/*
	 * do platform specific init: check the clock, grab/config pins, etc.
	 */
	if (pdata->platform_init && pdata->platform_init(pdev)) {
		retval = -ENODEV;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			pdev->dev.bus_id);
		return -ENODEV;
	}
	irq = res->start;

	fsl_platform_set_vbus_power(pdata, 1);

	hcd = usb_create_hcd(driver, &pdev->dev, pdev->dev.bus_id);
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	hcd->rsrc_start = pdata->r_start;
	hcd->rsrc_len = pdata->r_len;
	hcd->regs = pdata->regs;
	vdbg("rsrc_start=0x%llx rsrc_len=0x%llx virtual=0x%x\n",
	     hcd->rsrc_start, hcd->rsrc_len, hcd->regs);

	hcd->power_budget = pdata->power_budget;

	/* DDD
	 * the following must be done by this point, otherwise the OTG
	 * host port doesn't make it thru initializtion.
	 * ehci_halt(), called by ehci_fsl_setup() returns -ETIMEDOUT
	 */
	fsl_platform_set_host_mode(hcd);

	ehci_arc_dev = &pdev->dev;
	if (sysfs_create_file(&ehci_arc_dev->kobj, &dev_attr_wakeup_enable.attr) < 0)
		printk (KERN_ERR "ehci: could not create wakeup_value sysfs entry\n");

	if (sysfs_create_file(&ehci_arc_dev->kobj, &dev_attr_suspend_counter.attr) < 0)
		printk (KERN_ERR "ehci: could not create suspend_counter sysfs entry\n");

	retval = usb_add_hcd(hcd, irq, IRQF_DISABLED);
	if (retval != 0) {
		pr_debug("failed with usb_add_hcd\n");
		goto err2;
	}

	ehci = hcd_to_ehci(hcd);

#if defined(CONFIG_USB_OTG)
	if (pdata->does_otg) {
		dbg("pdev=0x%p  hcd=0x%p  ehci=0x%p\n", pdev, hcd, ehci);

		ehci->transceiver = otg_get_transceiver();
		dbg("ehci->transceiver=0x%p\n", ehci->transceiver);

		if (ehci->transceiver) {
			retval = otg_set_host(ehci->transceiver,
					      &ehci_to_hcd(ehci)->self);
			if (retval) {
				if (ehci->transceiver)
					put_device(ehci->transceiver->dev);
				goto err2;
			}
		} else {
			printk(KERN_ERR "can't find transceiver\n");
			retval = -ENODEV;
			goto err2;
		}
	}
#endif

	return retval;

      err2:
	usb_put_hcd(hcd);
      err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", pdev->dev.bus_id, retval);
	if (pdata->platform_uninit)
		pdata->platform_uninit(pdata);
	return retval;
}

static void usb_hcd_fsl_remove(struct usb_hcd *hcd,
			       struct platform_device *pdev)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;
	static struct device *ehci_arc_dev;

	dbg("%s  hcd=0x%p\n", __FUNCTION__, hcd);

	/* DDD shouldn't we turn off the power here? */
	fsl_platform_set_vbus_power(pdata, 0);

	usb_remove_hcd(hcd);

	if (ehci->transceiver) {
		(void)otg_set_host(ehci->transceiver, 0);
		put_device(ehci->transceiver->dev);
	}
	usb_put_hcd(hcd);

	ehci_arc_dev = &pdev->dev;
	sysfs_remove_file(&ehci_arc_dev->kobj, &dev_attr_wakeup_enable.attr);
	sysfs_remove_file(&ehci_arc_dev->kobj, &dev_attr_suspend_counter.attr);

	/*
	 * do platform specific un-initialization:
	 * release iomux pins, etc.
	 */
	if (pdata->platform_uninit)
		pdata->platform_uninit(pdata);
}

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_fsl_reinit(struct ehci_hcd *ehci)
{
	fsl_platform_usb_setup(ehci_to_hcd(ehci));
	ehci_port_power(ehci, 0);

	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_fsl_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
	    HC_LENGTH(ehci_readl(ehci, &ehci->caps->hc_capbase));

	vdbg("%s(): ehci->caps=0x%p  ehci->regs=0x%p\n", __FUNCTION__,
	     ehci->caps, ehci->regs);

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci->is_tdi_rh_tt = 1;

	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	retval = ehci_fsl_reinit(ehci);
	return retval;
}

/* USB EHCI bus suspend */
static int ehci_arc_bus_suspend(struct usb_hcd *hcd)
{
	/*
	 * Check if bus already suspended. If yes, then no need to redo suspend
	 */
	if (!wakeup_value) {
		/* Call the hub suspend now */
		ehci_bus_suspend(hcd);
	}

	return 0;
}

/* USB EHCI bus resume */
static int ehci_arc_bus_resume(struct usb_hcd *hcd)
{
	/* Call the hub resume now */
	ehci_bus_resume(hcd);
	return 0;
}

/* *INDENT-OFF* */
static const struct hc_driver ehci_arc_hc_driver = {
	.description	= hcd_name,
	.product_desc	= "Freescale On-Chip EHCI Host Controller",
	.hcd_priv_size	= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq		= ehci_irq,
	.flags		= FSL_PLATFORM_HC_FLAGS,

	/*
	 * basic lifecycle operations
	 */
	.reset		= ehci_fsl_setup,
	.start		= ehci_run,
	.stop		= ehci_stop,
	.shutdown	= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_arc_bus_suspend,
	.bus_resume		= ehci_arc_bus_resume,
};
/* *INDENT-ON* */

volatile static struct ehci_regs usb_ehci_regs;

#ifdef CONFIG_PM
/* suspend/resume, section 4.3 */

volatile static struct ehci_regs usb_ehci_regs;
static u32 usb_ehci_portsc;

/* These routines rely on the bus (pci, platform, etc)
 * to handle powerdown and wakeup, and currently also on
 * transceivers that don't need any software attention to set up
 * the right sort of wakeup.
 *
 * They're also used for turning on/off the port when doing OTG.
 */
static int ehci_arc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 cmd, tmp;
	
	pr_debug("%s pdev=0x%p  ehci=0x%p  hcd=0x%p\n",
		 __FUNCTION__, pdev, ehci, hcd);
	pr_debug("%s ehci->regs=0x%p  hcd->regs=0x%p  hcd->state=%d\n",
		 __FUNCTION__, ehci->regs, hcd->regs, hcd->state);

	hcd->state = HC_STATE_SUSPENDED;
	pdev->dev.power.power_state = PMSG_SUSPEND;

	if (hcd->driver->suspend)
		return hcd->driver->suspend(hcd, state);

	/* Clear the wakeup_value to stop idle */
	wakeup_value = 0;

	/* Cancel the low power idle workqueue */
	cancel_rearming_delayed_work(&ehci->dwork);

	/* ignore non-host interrupts */
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	cmd = ehci_readl(ehci, &ehci->regs->command);
	cmd &= ~CMD_RUN;
	ehci_writel(ehci, cmd, &ehci->regs->command);

	/* save EHCI registers */
	usb_ehci_regs.command = ehci_readl(ehci, &ehci->regs->command);
	usb_ehci_regs.status = ehci_readl(ehci, &ehci->regs->status);
	usb_ehci_regs.intr_enable = ehci_readl(ehci, &ehci->regs->intr_enable);
	usb_ehci_regs.frame_index = ehci_readl(ehci, &ehci->regs->frame_index);
	usb_ehci_regs.segment = ehci_readl(ehci, &ehci->regs->segment);
	usb_ehci_regs.frame_list = ehci_readl(ehci, &ehci->regs->frame_list);
	usb_ehci_regs.async_next = ehci_readl(ehci, &ehci->regs->async_next);
	usb_ehci_regs.configured_flag =
		ehci_readl(ehci, &ehci->regs->configured_flag);
	usb_ehci_portsc = ehci_readl(ehci, &ehci->regs->port_status[0]);
	
	/* clear the W1C bits */
	usb_ehci_portsc &= cpu_to_le32(~PORT_RWC_BITS);
	
	/* clear PP to cut power to the port */
	tmp = ehci_readl(ehci, &ehci->regs->port_status[0]);
	tmp &= ~PORT_POWER;
	ehci_writel(ehci, tmp, &ehci->regs->port_status[0]);

	/* Gate the PHY clock */
	tmp = ehci_readl(ehci, &ehci->regs->port_status[0]);
	tmp |= PORT_PHCD;
	ehci_writel(ehci, tmp, &ehci->regs->port_status[0]);

	return 0;
}

static int ehci_arc_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 tmp;
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;

	dbg("%s pdev=0x%p  pdata=0x%p  ehci=0x%p  hcd=0x%p\n",
	    __FUNCTION__, pdev, pdata, ehci, hcd);

	vdbg("%s ehci->regs=0x%p  hcd->regs=0x%p  usbmode=0x%x\n",
	     __FUNCTION__, ehci->regs, hcd->regs, pdata->usbmode);

	/* set host mode */
	tmp = USBMODE_CM_HOST | (pdata->es ? USBMODE_ES : 0);
	ehci_writel(ehci, tmp, hcd->regs + FSL_SOC_USB_USBMODE);

	/* restore EHCI registers */
	ehci_writel(ehci, usb_ehci_regs.command, &ehci->regs->command);
	ehci_writel(ehci, usb_ehci_regs.intr_enable, &ehci->regs->intr_enable);
	ehci_writel(ehci, usb_ehci_regs.frame_index, &ehci->regs->frame_index);
	ehci_writel(ehci, usb_ehci_regs.segment, &ehci->regs->segment);
	ehci_writel(ehci, usb_ehci_regs.frame_list, &ehci->regs->frame_list);
	ehci_writel(ehci, usb_ehci_regs.async_next, &ehci->regs->async_next);
	ehci_writel(ehci, usb_ehci_regs.configured_flag,
			&ehci->regs->configured_flag);
	ehci_writel(ehci, usb_ehci_regs.frame_list, &ehci->regs->frame_list);
	ehci_writel(ehci, usb_ehci_portsc, &ehci->regs->port_status[0]);

	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	hcd->state = HC_STATE_RUNNING;
	pdev->dev.power.power_state = PMSG_ON;

	tmp = ehci_readl(ehci, &ehci->regs->command);
	tmp |= CMD_RUN;
	ehci_writel(ehci, tmp, &ehci->regs->command);

	usb_hcd_resume_root_hub(hcd);

	/* Enter low power IDLE now */	
	wakeup_value = 1;

	/* Restart the workqueue */
	schedule_delayed_work(&ehci->dwork, msecs_to_jiffies(EHCI_IDLE_SUSPEND_THRESHOLD));

	return 0;
}
#endif

static int ehci_hcd_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_fsl_probe(&ehci_arc_hc_driver, pdev);
}

static int __init_or_module ehci_hcd_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_fsl_remove(hcd, pdev);

	return 0;
}

/* *INDENT-OFF* */
static struct platform_driver ehci_fsl_driver = {
	.probe   = ehci_hcd_drv_probe,
	.remove  = ehci_hcd_drv_remove,
#ifdef CONFIG_PM
	.suspend = ehci_arc_suspend,
	.resume = ehci_arc_resume,
#endif
	.driver  = {
			.name = "fsl-ehci",
		   },
};
/* *INDENT-ON* */
