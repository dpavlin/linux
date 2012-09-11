/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Support charger mode and gadget mode fixups
 * Copyright 2009 Lab126.com
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
 * @file arcotg_udc.c
 * @brief arc otg device controller driver
 * @ingroup USB
 */

/*!
 * Include files
 */

/* 
 * The HOST detection is done via the PMIC_IRQ for the charger. During
 * this time, the ARCOTG_IRQ stays disabled. It is only enabled in
 * fsl_low_power_resume() that gets invokved by the charger. this way, 
 * the driver is in sync with the charger
 */

/*
 * In case of HOST enumeration failure, we need to do a full suspend/resume
 * cycle, i.e. redo the dr_controller_{setup/run}. During this process, the
 * PMIC_IRQ stays disabled as we dont want the charger specific events invoking
 * arcotg_udc calls that stomp on the suspend/resume.
 */

#if 0
#define	DEBUG
#define VERBOSE
#define DUMP_QUEUES
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/usb/ch9.h>
#include <linux/usb_gadget.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/fsl_devices.h>
#include <linux/usb/fsl_xcvr.h>
#include <linux/usb/otg.h>
#include <linux/clk.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/cpufreq.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/arch/pmic_external.h>
#include <asm/arch/pmic_power.h>

#include "arcotg_udc.h"
#include <asm/arch/arc_otg.h>
#include <asm/arch/pmic_external.h>
#define LLOG_KERNEL_COMP "arcotg_udc"
#include <llog.h>

#undef pr_debug
#define pr_debug(fmt, ...) LLOGS_DEBUG9("", fmt, ##__VA_ARGS__)
#define UDC_LOG(lvl, desc, fmt, ...) LLOG_##lvl(desc, "", "%s - " fmt, __func__, ##__VA_ARGS__)
#define LOG_DPDM(lvl, desc, fmt, dp, dm, ...) LLOG_##lvl(desc, "D+=%d,D-=%d", "%s - " fmt, dp, dm, __func__, ##__VA_ARGS__)
#define LOG_PHY(lvl, desc, fmt, phyid, ...) LLOG_##lvl(desc, "phyid=%#04x", "%s - " fmt, phyid, __func__, ##__VA_ARGS__)

extern int gpio_usbotg_hs_active(void);
extern int gpio_usbotg_fs_active(void);
extern void mxc_set_gpio_dataout(iomux_pin_name_t, u32);

#ifdef CONFIG_MACH_LAB126
extern int charger_set_current_limit(int new_limit);
extern int charger_handle_charging(void);
extern void charger_set_arcotg_callback(int (*callback)(void *, int), void *arg);
extern int charger_misdetect_retry;
extern int charger_probed;
#endif

static void ep0stall(struct arcotg_udc *);
static int ep0_prime_status(struct arcotg_udc *, int);

static int timeout;
static struct clk *usb_ahb_clk;
static void chgdisc_event(struct arcotg_udc *udc);

/* Has the charger timer been deleted? */
static int udc_charger_timer_deleted = 0;

#undef	USB_TRACE

#define	DRIVER_DESC	"ARC USBOTG Device Controller driver"
#define	DRIVER_AUTHOR	"Manish Lachwani, Lab126.com"
#define	DRIVER_VERSION	"2009-05-11"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)

#define PMIC_IRQ 67	/* PMIC IRQ */
#define ARCOTG_IRQ 37	/* ARCOTG IRQ */

#ifdef CONFIG_MACH_LAB126
static uint recovery_mode;
module_param(recovery_mode, uint, S_IRUGO);
MODULE_PARM_DESC(recovery_mode, "driver is used in recovery mode");
static uint force_fs;
module_param(force_fs, uint, S_IRUGO);
MODULE_PARM_DESC(force_fs, "force device to run at full speed");
#else
/*#define DEBUG_FORCE_FS 1 */
#endif

static const char driver_name[] = "arc_udc";
static const char driver_desc[] = DRIVER_DESC;

volatile static struct usb_dr_device *usb_slave_regs;

/* it is initialized in probe()  */
static struct arcotg_udc *udc_controller;
atomic_t arcotg_busy = ATOMIC_INIT(0);
static int udc_full_sr = 0;

static void do_charger_detect_work(struct work_struct *work);
static void reset_irq(struct arcotg_udc *udc);

/*ep name is important in gadget, it should obey the convention of ep_match()*/
/* even numbered EPs are OUT or setup, odd are IN/INTERRUPT */
static const char *const ep_name[] = {
	"ep0-control", NULL,	/* everyone has ep0 */
	/* 7 configurable endpoints */
	"ep1out",
	"ep1in",
	"ep2out",
	"ep2in",
	"ep3out",
	"ep3in",
	"ep4out",
	"ep4in",
	"ep5out",
	"ep5in",
	"ep6out",
	"ep6in",
	"ep7out",
	"ep7in"
};
static const struct usb_endpoint_descriptor arcotg_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = 0,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize = USB_MAX_CTRL_PAYLOAD,
};

static int udc_suspend(struct arcotg_udc *udc);
static int fsl_udc_suspend(struct device *dev, pm_message_t state);
static int fsl_udc_resume(struct device *dev);
unsigned int g_lab126_log_mask;
static ssize_t
show_log_mask(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t
store_log_mask(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR(log_mask, 0666, show_log_mask, store_log_mask);
#ifdef CONFIG_MACH_LAB126
static void fsl_udc_low_power_resume(struct arcotg_udc *udc);
static void wakeup_work(struct work_struct *work);
static ssize_t
show_connected(struct device *dev, struct device_attribute *attr, char *buf);
static DEVICE_ATTR(connected, 0444, show_connected, NULL);
static ssize_t
show_phy_manufacturer(struct device *dev, struct device_attribute *attr, char *buf);
static DEVICE_ATTR(manufacturer, 0444, show_phy_manufacturer, NULL);
atomic_t port_reset_counter = ATOMIC_INIT(0);
extern void isp1504_set(u8 bits, int reg, volatile u32 * view);
extern u8 isp1504_read(int reg, volatile u32 * view);

#define NXP_ID	0x04CC
static void reset_nxp_phy(void);
#define SMSC_ID	0x0424
static void reset_smsc_phy(void);

static void arcotg_full_resume(struct arcotg_udc *udc);
static void arcotg_full_suspend(struct arcotg_udc *udc);

static struct phy_ops {
	u16 id;
	void (*reset)(void);
	char name[8];
} phy_list[] = {
	{
		.id = NXP_ID,
		.reset = reset_nxp_phy,
		.name = "NXP",
	},
	{
		.id = SMSC_ID,
		.reset = reset_smsc_phy,
		.name = "SMSC",
	},
};

static struct phy_ops *local_phy_ops = NULL;

#endif

/********************************************************************
 *	Internal Used Function
********************************************************************/

#ifdef DUMP_QUEUES
static void dump_ep_queue(struct arcotg_ep *ep)
{
	int ep_index;
	struct arcotg_req *req;
	struct ep_td_struct *dtd;

	if (list_empty(&ep->queue)) {
		pr_debug("udc empty\n");
		return;
	}

	ep_index = ep_index(ep) * 2 + ep_is_in(ep);
	pr_debug("udc ep=0x%p  index=%d\n", ep, ep_index);

	list_for_each_entry(req, &ep->queue, queue) {
		pr_debug("udc req=0x%p  dTD count=%d\n", req, req->dtd_count);
		pr_debug("udc dTD head=0x%p  tail=0x%p\n", req->head,
			 req->tail);

		dtd = req->head;

		while (dtd) {
			if (le32_to_cpu(dtd->next_td_ptr) & DTD_NEXT_TERMINATE)
				break;	/* end of dTD list */

			dtd = dtd->next_td_virt;
		}
	}
}
#else
static inline void dump_ep_queue(struct arcotg_ep *ep)
{
}
#endif

extern atomic_t usb_dma_doze_ref_count;
atomic_t usb_clk_change = ATOMIC_INIT(0);

#ifdef CONFIG_CPU_FREQ

/*
 * The usb_ahb_clk is sensitive to the cpu_clk changes. This will
 * make sure that the CPUFreq notifies the arcotg of the PRECHANGE
 * and POSTCHANGE events. When CPU is scaling, the usb_ahb_clk should
 * not be touched as it will also undergo changes
 */
int arcotg_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	switch (val) {
	case CPUFREQ_PRECHANGE:
		atomic_set(&usb_clk_change, 1);
		break;
	case CPUFREQ_POSTCHANGE:
		atomic_set(&usb_clk_change, 0);
		break;
	}

	return 0;
}
#endif

/*!
 * done() - retire a request; caller blocked irqs
 * @param ep      endpoint pointer
 * @param req     require pointer
 * @param status  when req->req.status is -EINPROGRESSS, it is input para
 *	     else it will be a output parameter
 * req->req.status : in ep_queue() it will be set as -EINPROGRESS
 */
static void done(struct arcotg_ep *ep, struct arcotg_req *req, int status)
{
	struct arcotg_udc *udc;
	unsigned char stopped = ep->stopped;

	udc = (struct arcotg_udc *)ep->udc;

	pr_debug("udc req=0x%p\n", req);
	if (req->head) {
		pr_debug("udc freeing head=0x%p\n", req->head);
		dma_pool_free(udc->dtd_pool, req->head, req->head->td_dma);
	}

	/* the req->queue pointer is used by ep_queue() func, in which
	 * the request will be added into a udc_ep->queue 'd tail
	 * so here the req will be dropped from the ep->queue
	 */
	list_del_init(&req->queue);

	/* req.status should be set as -EINPROGRESS in ep_queue() */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	pr_debug("udc req=0x%p  mapped=%x\n", req, req->mapped);

	if (req->mapped) {
		pr_debug("udc calling dma_unmap_single(buf,%s)  req=0x%p  "
			 "a=0x%x  len=%d\n",
			 ep_is_in(ep) ? "to_dvc" : "from_dvc",
			 req, req->req.dma, req->req.length);

		dma_unmap_single(ep->udc->gadget.dev.parent,
				 req->req.dma, req->req.length,
				 ep_is_in(ep) ? DMA_TO_DEVICE :
				 DMA_FROM_DEVICE);

		req->req.dma = DMA_ADDR_INVALID;
		req->mapped = 0;
		pr_debug("udc req=0x%p set req.dma=0x%x\n", req, req->req.dma);
	} else {
		if ((req->req.length != 0)
		    && (req->req.dma != DMA_ADDR_INVALID)) {
			pr_debug("udc calling dma_sync_single_for_cpu(buf,%s) "
				 "req=0x%p  dma=0x%x  len=%d\n",
				 ep_is_in(ep) ? "to_dvc" : "from_dvc", req,
				 req->req.dma, req->req.length);

			dma_sync_single_for_cpu(ep->udc->gadget.dev.parent,
						req->req.dma, req->req.length,
						ep_is_in(ep) ? DMA_TO_DEVICE :
						DMA_FROM_DEVICE);
		}
	}

	if (status && (status != -ESHUTDOWN)) {
		pr_debug("udc complete %s req 0c%p stat %d len %u/%u\n",
			 ep->ep.name, &req->req, status,
			 req->req.actual, req->req.length);
	}

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;

	spin_unlock(&ep->udc->lock);

	/* this complete() should a func implemented by gadget layer,
	 * eg fsg->bulk_in_complete() */
	if (req->req.complete) {
		pr_debug("udc calling gadget's complete()  req=0x%p\n", req);
		req->req.complete(&ep->ep, &req->req);
		pr_debug("udc back from gadget's complete()\n");
	}

	spin_lock(&ep->udc->lock);
	ep->stopped = stopped;
}

/*!
 * nuke(): delete all requests related to this ep
 * called by ep_disable() within spinlock held
 * add status paramter?
 * @param ep endpoint pointer
 * @param status current status
 */
static void nuke(struct arcotg_ep *ep, int status)
{
	pr_debug("udc ep=0x%p\n", ep);
	ep->stopped = 1;

	/* Whether this eq has request linked */
	while (!list_empty(&ep->queue)) {
		struct arcotg_req *req = list_entry(ep->queue.next, struct arcotg_req, queue);

		done(ep, req, status);
	}
	dump_ep_queue(ep);
}

/*------------------------------------------------------------------
	Internal Hardware related function
 ------------------------------------------------------------------*/
extern void fsl_platform_set_vbus_power(struct fsl_usb2_platform_data *pdata,
					int on);

/*
 * init device controller
 * @param  qh_addr the aligned virt addr of ep QH addr
 * @param  dev     device controller pointer
 */
static int dr_controller_setup(struct arcotg_udc *udc)
{
	unsigned int tmp = 0, portctrl = 0;
	void *qh_addr = udc->ep_qh;
	struct device *dev __attribute((unused)) = udc->gadget.dev.parent;
	struct fsl_usb2_platform_data *pdata;

	pdata = udc->pdata;

	pr_debug("udc dev=0x%p\n", dev);

	/* before here, make sure usb_slave_regs has been initialized */
	if (!qh_addr)
		return -EINVAL;

	/* Stop and reset the usb controller */
	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp &= ~USB_CMD_RUN_STOP;
	usb_slave_regs->usbcmd = cpu_to_le32(tmp);

	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp |= USB_CMD_CTRL_RESET;
	usb_slave_regs->usbcmd = cpu_to_le32(tmp);

	/* Wait for reset to complete */
	timeout = 10000000;
	while ((le32_to_cpu(usb_slave_regs->usbcmd) & USB_CMD_CTRL_RESET) &&
	       --timeout) {
		continue;
	}
	if (timeout == 0) {
		UDC_LOG(ERROR, "to1", "TIMEOUT\n");
		return -ETIMEDOUT;
	}

	/* Set the controller as device mode */
	tmp = le32_to_cpu(usb_slave_regs->usbmode);
	tmp |= USB_MODE_CTRL_MODE_DEVICE;
	/* Disable Setup Lockout */
	tmp |= USB_MODE_SETUP_LOCK_OFF;
	usb_slave_regs->usbmode = cpu_to_le32(tmp);

	if (pdata->xcvr_ops && pdata->xcvr_ops->set_device)
		pdata->xcvr_ops->set_device();

	/* Clear the setup status */
	usb_slave_regs->usbsts = 0;

	tmp = udc->ep_qh_dma;
	tmp &= USB_EP_LIST_ADDRESS_MASK;
	usb_slave_regs->endpointlistaddr = cpu_to_le32(tmp);

	pr_debug("udc vir[qh_base]=0x%p phy[qh_base]=0x%8x epla_reg=0x%8x\n",
		 qh_addr, (int)tmp,
		 le32_to_cpu(usb_slave_regs->endpointlistaddr));

	portctrl = le32_to_cpu(usb_slave_regs->portsc1);
	portctrl &= ~PORTSCX_PHY_TYPE_SEL;

	portctrl |= udc->xcvr_type;

#ifdef CONFIG_MACH_LAB126
	UDC_LOG(INFO, "fs", "force_fs = %d\n", force_fs);
#else
#ifdef DEBUG_FORCE_FS
	portctrl |= 0x1000000;
#endif
#endif

	usb_slave_regs->portsc1 = cpu_to_le32(portctrl);

	if (NXP_ID == local_phy_ops->id)
		fsl_platform_set_vbus_power(pdata, 0);

	return 0;
}

/*!
 * just Enable DR irq reg and Set Dr controller Run 
 * @param  udc  device controller pointer
 */

static void dr_controller_run(struct arcotg_udc *udc)
{
	u32 tmp;

	pr_debug("%s\n", __FUNCTION__);

	/*Enable DR irq reg */
	tmp = USB_INTR_INT_EN | USB_INTR_ERR_INT_EN |
	    USB_INTR_PTC_DETECT_EN | USB_INTR_RESET_EN |
	    USB_INTR_DEVICE_SUSPEND | USB_INTR_SYS_ERR_EN;

	usb_slave_regs->usbintr = le32_to_cpu(tmp);

	/* Clear stopped bit */
	udc->stopped = 0;

	/* Set controller to Run */
	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp |= USB_CMD_RUN_STOP;
	usb_slave_regs->usbcmd = le32_to_cpu(tmp);

	return;
}

/*
 * just Disable DR irq reg and Set Dr controller Stop
 * @param  udc  device controller pointer
 */
static void dr_controller_stop(struct arcotg_udc *udc)
{
	unsigned int tmp;

	pr_debug("%s\n", __FUNCTION__);

	/* if we're in OTG mode, and the Host is currently using the port,
	 * stop now and don't rip the controller out from under the
	 * ehci driver
	 */
	if (udc->gadget.is_otg) {
		if (!(usb_slave_regs->otgsc & OTGSC_STS_USB_ID)) {
			pr_debug("udc Leaving early\n");
			return;
		}
	}

	/* disable all INTR */
	usb_slave_regs->usbintr = 0;

	/* Set stopped bit for isr */
	udc->stopped = 1;

	/* set controller to Stop */
	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp &= ~USB_CMD_RUN_STOP;
	usb_slave_regs->usbcmd = le32_to_cpu(tmp);
}

void dr_ep_setup(unsigned char ep_num, unsigned char dir, unsigned char ep_type)
{
	unsigned int tmp_epctrl = 0;

	tmp_epctrl = le32_to_cpu(usb_slave_regs->endptctrl[ep_num]);
	if (dir) {
		if (ep_num)
			tmp_epctrl |= EPCTRL_TX_DATA_TOGGLE_RST;
		tmp_epctrl |= EPCTRL_TX_ENABLE;
		tmp_epctrl |=
		    ((unsigned int)(ep_type) << EPCTRL_TX_EP_TYPE_SHIFT);
	} else {
		if (ep_num)
			tmp_epctrl |= EPCTRL_RX_DATA_TOGGLE_RST;
		tmp_epctrl |= EPCTRL_RX_ENABLE;
		tmp_epctrl |=
		    ((unsigned int)(ep_type) << EPCTRL_RX_EP_TYPE_SHIFT);
	}

	usb_slave_regs->endptctrl[ep_num] = cpu_to_le32(tmp_epctrl);

	/* wait for the write reg to finish */
	timeout = 10000000;
	while ((!(le32_to_cpu(usb_slave_regs->endptctrl[ep_num]) &
		  (tmp_epctrl & (EPCTRL_TX_ENABLE | EPCTRL_RX_ENABLE))))
	       && --timeout) {
		continue;
	}
	if (timeout == 0) {
		UDC_LOG(ERROR, "to2", "TIMEOUT\n");
	}
}

static void dr_ep_change_stall(unsigned char ep_num, unsigned char dir,
			       int value)
{
	unsigned int tmp_epctrl = 0;

	tmp_epctrl = le32_to_cpu(usb_slave_regs->endptctrl[ep_num]);

	if (value) {
		/* set the stall bit */
		if (dir)
			tmp_epctrl |= EPCTRL_TX_EP_STALL;
		else
			tmp_epctrl |= EPCTRL_RX_EP_STALL;
	} else {
		/* clear the stall bit and reset data toggle */
		if (dir) {
			tmp_epctrl &= ~EPCTRL_TX_EP_STALL;
			tmp_epctrl |= EPCTRL_TX_DATA_TOGGLE_RST;
		} else {
			tmp_epctrl &= ~EPCTRL_RX_EP_STALL;
			tmp_epctrl |= EPCTRL_RX_DATA_TOGGLE_RST;
		}
	}
	usb_slave_regs->endptctrl[ep_num] = cpu_to_le32(tmp_epctrl);
}

/* Get stall status of a specific ep
   Return: 0: not stalled; 1:stalled */
static int dr_ep_get_stall(unsigned char ep_num, unsigned char dir)
{
	u32 epctrl;

	epctrl = le32_to_cpu(usb_slave_regs->endptctrl[ep_num]);
	if (dir)
		return (epctrl & EPCTRL_TX_EP_STALL) ? 1 : 0;
	else
		return (epctrl & EPCTRL_RX_EP_STALL) ? 1 : 0;
}

/********************************************************************
	Internal Structure Build up functions
********************************************************************/

/*!
 * set the Endpoint Capabilites field of QH
 * @param handle  udc handler
 * @param ep_num  endpoint number
 * @param dir     in or out
 * @param ep_type USB_ENDPOINT_XFER_CONTROL or other
 * @param max_pkt_len  max packet length of this endpoint
 * @param zlt   Zero Length Termination Select
 * @param mult  Mult field
 */
static void struct_ep_qh_setup(void *handle, unsigned char ep_num,
			       unsigned char dir, unsigned char ep_type,
			       unsigned int max_pkt_len,
			       unsigned int zlt, unsigned char mult)
{
	struct arcotg_udc *udc;
	struct ep_queue_head *p_QH;
	unsigned int tmp = 0;

	udc = (struct arcotg_udc *)handle;

	p_QH = &udc->ep_qh[2 * ep_num + dir];

	/* set the Endpoint Capabilites Reg of QH */
	switch (ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
		/* Interrupt On Setup (IOS). for control ep  */
		tmp = (max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS) |
		    EP_QUEUE_HEAD_IOS;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		tmp = (max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS) |
		    (mult << EP_QUEUE_HEAD_MULT_POS);
		break;
	case USB_ENDPOINT_XFER_BULK:
	case USB_ENDPOINT_XFER_INT:
		tmp = max_pkt_len << EP_QUEUE_HEAD_MAX_PKT_LEN_POS;
		if (zlt)
			tmp |= EP_QUEUE_HEAD_ZLT_SEL;
		break;
	default:
		pr_debug("udc error ep type is %d\n", ep_type);
		return;
	}
	p_QH->max_pkt_length = le32_to_cpu(tmp);

	return;
}

/*! 
 * This function only to make code looks good
 * it is a collection of struct_ep_qh_setup and dr_ep_setup for ep0
 * ep0 should set OK before the bind() of gadget layer
 * @param udc  device controller pointer
 */
static void ep0_dr_and_qh_setup(struct arcotg_udc *udc)
{
	/* the intialization of an ep includes: fields in QH, Regs,
	 * arcotg_ep struct */
	struct_ep_qh_setup(udc, 0, USB_RECV,
			   USB_ENDPOINT_XFER_CONTROL, USB_MAX_CTRL_PAYLOAD,
			   0, 0);
	struct_ep_qh_setup(udc, 0, USB_SEND,
			   USB_ENDPOINT_XFER_CONTROL, USB_MAX_CTRL_PAYLOAD,
			   0, 0);
	dr_ep_setup(0, USB_RECV, USB_ENDPOINT_XFER_CONTROL);
	dr_ep_setup(0, USB_SEND, USB_ENDPOINT_XFER_CONTROL);

	return;

}

#ifdef CONFIG_MACH_LAB126
static void arcotg_reschedule_work(struct arcotg_udc *udc)
{
	fsl_udc_low_power_resume(udc);
	if (!udc->work_scheduled) {
		atomic_set(&udc->run_workqueue, 1);
		schedule_delayed_work(&udc->work, msecs_to_jiffies(30000));
		udc->work_scheduled = 1;
	}
}
#endif

/***********************************************************************
		Endpoint Management Functions
***********************************************************************/

/*!
 * when configurations are set, or when interface settings change
 * for example the do_set_interface() in gadget layer,
 * the driver will enable or disable the relevant endpoints
 * ep0 will not use this func it is enable in probe()
 * @param _ep   endpoint pointer
 * @param desc  endpoint descriptor pointer
 * @return The function returns 0 on success or -1 if failed
 */
static int arcotg_ep_enable(struct usb_ep *_ep,
			    const struct usb_endpoint_descriptor *desc)
{
	struct arcotg_udc *udc;
	struct arcotg_ep *ep;
	unsigned short max = 0;
	unsigned char mult = 0, zlt = 0;
	int retval = 0;
	unsigned long flags = 0;
	char *val;	/* for debug */

	ep = container_of(_ep, struct arcotg_ep, ep);

	pr_debug("udc %s ep.name=%s\n", __FUNCTION__, ep->ep.name);
	/* catch various bogus parameters */
	if (!_ep || !desc || ep->desc || _ep->name == ep_name[0] ||
	    (desc->bDescriptorType != USB_DT_ENDPOINT))
		/* FIXME: add judge for ep->bEndpointAddress */
		return -EINVAL;

	udc = ep->udc;

	if (!udc->driver || (udc->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif

	max = le16_to_cpu(desc->wMaxPacketSize);
	retval = -EINVAL;

	/* check the max package size validate for this endpoint */
	/* Refer to USB2.0 spec table 9-13,
	 */
	switch (desc->bmAttributes & 0x03) {
	case USB_ENDPOINT_XFER_BULK:
		if (strstr(ep->ep.name, "-iso")
		    || strstr(ep->ep.name, "-int"))
			goto en_done;
		mult = 0;
		zlt = 1;
		switch (udc->gadget.speed) {
		case USB_SPEED_HIGH:
			if ((max == 128) || (max == 256) || (max == 512))
				break;
		default:
			switch (max) {
			case 4:
			case 8:
			case 16:
			case 32:
			case 64:
				break;
			default:
			case USB_SPEED_LOW:
				goto en_done;
			}
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		if (strstr(ep->ep.name, "-iso"))	/* bulk is ok */
			goto en_done;
		mult = 0;
		zlt = 1;
		switch (udc->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
		case USB_SPEED_FULL:
			if (max <= 64)
				break;
		default:
			if (max <= 8)
				break;
			goto en_done;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (strstr(ep->ep.name, "-bulk") || strstr(ep->ep.name, "-int"))
			goto en_done;
		mult = (unsigned char)
		    (1 + ((le16_to_cpu(desc->wMaxPacketSize) >> 11) & 0x03));
		zlt = 0;
		switch (udc->gadget.speed) {
		case USB_SPEED_HIGH:
			if (max <= 1024)
				break;
		case USB_SPEED_FULL:
			if (max <= 1023)
				break;
		default:
			goto en_done;
		}
		break;
	case USB_ENDPOINT_XFER_CONTROL:
		if (strstr(ep->ep.name, "-iso") || strstr(ep->ep.name, "-int"))
			goto en_done;
		mult = 0;
		zlt = 1;
		switch (udc->gadget.speed) {
		case USB_SPEED_HIGH:
		case USB_SPEED_FULL:
			switch (max) {
			case 1:
			case 2:
			case 4:
			case 8:
			case 16:
			case 32:
			case 64:
				break;
			default:
				goto en_done;
			}
		case USB_SPEED_LOW:
			switch (max) {
			case 1:
			case 2:
			case 4:
			case 8:
				break;
			default:
				goto en_done;
			}
		default:
			goto en_done;
		}
		break;

	default:
		goto en_done;
	}

	/* here initialize variable of ep */
	spin_lock_irqsave(&udc->lock, flags);
	ep->ep.maxpacket = max;
	ep->desc = desc;
	ep->stopped = 0;

	/* hardware special operation */

	/* Init EPx Queue Head (Ep Capabilites field in QH
	 * according to max, zlt, mult) */
	struct_ep_qh_setup((void *)udc, (unsigned char)ep_index(ep),
			   (unsigned char)
			   ((desc->bEndpointAddress & USB_DIR_IN) ?
			    USB_SEND : USB_RECV), (unsigned char)
			   (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK),
			   max, zlt, mult);

	/* Init endpoint x at here */
	/* 83xx RM chapter 16.3.2.24, here init the endpoint ctrl reg */
	dr_ep_setup((unsigned char)ep_index(ep),
		    (unsigned char)((desc->bEndpointAddress & USB_DIR_IN)
				    ? USB_SEND : USB_RECV),
		    (unsigned char)(desc->bmAttributes &
				    USB_ENDPOINT_XFERTYPE_MASK));

	/* Now HW will be NAKing transfers to that EP,
	 * until a buffer is queued to it. */

	/* should have stop the lock */
	spin_unlock_irqrestore(&udc->lock, flags);
	retval = 0;
	switch (desc->bmAttributes & 0x03) {
	case USB_ENDPOINT_XFER_BULK:
		val = "bulk";
		break;
	case USB_ENDPOINT_XFER_ISOC:
		val = "iso";
		break;
	case USB_ENDPOINT_XFER_INT:
		val = "intr";
		break;
	default:
		val = "ctrl";
		break;
	}

	pr_debug("udc enabled %s (ep%d%s-%s) maxpacket %d\n", ep->ep.name,
		 ep->desc->bEndpointAddress & 0x0f,
		 (desc->bEndpointAddress & USB_DIR_IN) ? "in" : "out",
		 val, max);
      en_done:
	return retval;
}

/*!
 * disable endpoint
 * Any pending and uncomplete req will complete with status (-ESHUTDOWN)
 * @param _ep  the ep being unconfigured. May not be ep0
 * @return The function returns 0 on success or -1 if failed
 */
static int arcotg_ep_disable(struct usb_ep *_ep)
{
	struct arcotg_udc *udc;
	struct arcotg_ep *ep;
	unsigned long flags = 0;

	ep = container_of(_ep, struct arcotg_ep, ep);
	if (!_ep || !ep->desc) {
		pr_debug("udc %s not enabled\n", _ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	udc = (struct arcotg_udc *)ep->udc;

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif

	spin_lock_irqsave(&udc->lock, flags);

	/* Nuke all pending requests (does flush) */
	nuke(ep, -ESHUTDOWN);

	ep->desc = 0;
	ep->stopped = 1;
	spin_unlock_irqrestore(&udc->lock, flags);

	pr_debug("udc disabled %s OK\n", _ep->name);
	return 0;
}

/*!
 * allocate a request object used by this endpoint
 * the main operation is to insert the req->queue to the eq->queue
 * @param _ep       the ep being unconfigured. May not be ep0
 * @param gfp_flags mem flags
 * @return Returns the request, or null if one could not be allocated
 */

static struct usb_request *arcotg_alloc_request_local(gfp_t gfp_flags)
{
	struct arcotg_req *req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);
	return &req->req;
}

static struct usb_request *arcotg_alloc_request(struct usb_ep *_ep,
						gfp_t gfp_flags)
{
	struct arcotg_req *req = kzalloc(sizeof *req, gfp_flags);
	if (!req)
		return NULL;

	req->req.dma = DMA_ADDR_INVALID;
	pr_debug("udc req=0x%p   set req.dma=0x%x\n", req, req->req.dma);
	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/*!
 * free request memory
 * @param _ep       the ep being unconfigured. May not be ep0
 * @param  _req     usb request pointer
 */
static void arcotg_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct arcotg_req *req;

	req = container_of(_req, struct arcotg_req, req);
	pr_debug("udc req=0x%p\n", req);

	if (_req)
		kfree(req);
}


/*-------------------------------------------------------------------------*/

/*!
 * link req's dTD queue to the end of ep's QH's dTD queue.
 * @param ep    endpoint pointer
 * @param req   request pointer
 * @return The function returns 0 on success or -1 if failed
 */
static int arcotg_queue_td(struct arcotg_ep *ep, struct arcotg_req *req)
{
	int i = ep_index(ep) * 2 + ep_is_in(ep);
	u32 temp, bitmask, tmp_stat;
	struct ep_queue_head *dQH = &ep->udc->ep_qh[i];

	pr_debug("udc queue req=0x%p to ep index %d\n", req, i);
	bitmask = (ep_is_in(ep)) ? (1 << (ep_index(ep) + 16)) :
	    (1 << (ep_index(ep)));

	/* check if the pipe is empty */
	if (!(list_empty(&ep->queue))) {
		/* Add td to the end */
		struct arcotg_req *lastreq;
		lastreq = list_entry(ep->queue.prev, struct arcotg_req, queue);

		WARN_ON(req->head->td_dma & 31);
		lastreq->tail->next_td_ptr = req->head->td_dma;
		lastreq->tail->next_td_virt = req->head;

		pr_debug("udc ep's queue not empty.  lastreq=0x%p\n", lastreq);

		/* Read prime bit, if 1 goto done */
		if (usb_slave_regs->endpointprime & cpu_to_le32(bitmask)) {
			pr_debug("udc ep's already primed\n");
			goto out;
		}

		timeout = 10000000;
		do {
			/* Set ATDTW bit in USBCMD */
			usb_slave_regs->usbcmd |= cpu_to_le32(USB_CMD_ATDTW);

			/* Read correct status bit */
			tmp_stat = le32_to_cpu(usb_slave_regs->endptstatus) &
			    bitmask;

		} while ((!(usb_slave_regs->usbcmd &
			    cpu_to_le32(USB_CMD_ATDTW)))
			 && --timeout);

		if (timeout == 0) {
			UDC_LOG(ERROR, "to3", "TIMEOUT\n");
		}

		/* Write ATDTW bit to 0 */
		usb_slave_regs->usbcmd &= cpu_to_le32(~USB_CMD_ATDTW);

		if (tmp_stat)
			goto out;

		/* fall through to Case 1: List is empty */
	}

	/* Write dQH next pointer and terminate bit to 0 */
	WARN_ON(req->head->td_dma & 31);
	dQH->next_dtd_ptr = cpu_to_le32(req->head->td_dma);

	/* Clear active and halt bit */
	temp = cpu_to_le32(~(EP_QUEUE_HEAD_STATUS_ACTIVE |
			     EP_QUEUE_HEAD_STATUS_HALT));
	dQH->size_ioc_int_sts &= temp;

	/* Prime endpoint by writing 1 to ENDPTPRIME */
	temp = (ep_is_in(ep)) ? (1 << (ep_index(ep) + 16)) :
	    (1 << (ep_index(ep)));

	pr_debug("udc setting endpointprime. temp=0x%x (bitmask=0x%x)\n",
		 temp, bitmask);
	usb_slave_regs->endpointprime |= cpu_to_le32(temp);

      out:
	return 0;
}

static int arcotg_build_dtd(struct arcotg_req *req, unsigned max,
			    struct ep_td_struct **address,
			    struct arcotg_udc *udc)
{
	unsigned length;
	u32 swap_temp;
	struct ep_td_struct *dtd;
	dma_addr_t handle;

	/* how big will this packet be? */
	length = min(req->req.length - req->req.actual, max);

	dtd = dma_pool_alloc(udc->dtd_pool, __GFP_WAIT, &handle);
	pr_debug("udc dma_pool_alloc() ep(0x%p)=%s  virt=0x%p  dma=0x%x\n",
		 req->ep, req->ep->name, dtd, handle);

	/* check alignment - must be 32 byte aligned (bits 4:0 == 0) */
	BUG_ON((u32) dtd & 31);
	BUG_ON(dtd == NULL);

	if (dtd != NULL) {
		memset(dtd, 0, sizeof(struct ep_td_struct));
		dtd->td_dma = handle;
	}

	/* Fill in the transfer size; set interrupt on every dtd;
	   set active bit */
	swap_temp = ((length << DTD_LENGTH_BIT_POS) | DTD_IOC
		     | DTD_STATUS_ACTIVE);

	dtd->size_ioc_sts = cpu_to_le32(swap_temp);

	/* Clear reserved field */
	swap_temp = cpu_to_le32(dtd->size_ioc_sts);
	swap_temp &= ~DTD_RESERVED_FIELDS;
	dtd->size_ioc_sts = cpu_to_le32(swap_temp);

	pr_debug("udc req=0x%p  dtd=0x%p  req.dma=0x%x  req.length=%d  "
		 "length=%d  size_ioc_sts=0x%x\n",
		 req, dtd, req->req.dma, req->req.length,
		 length, dtd->size_ioc_sts);

	/* Init all of buffer page pointers */
	swap_temp = (u32) (req->req.dma + req->req.actual);
	dtd->buff_ptr0 = cpu_to_le32(swap_temp);
	dtd->buff_ptr1 = cpu_to_le32(swap_temp + 0x1000);
	dtd->buff_ptr2 = cpu_to_le32(swap_temp + 0x2000);
	dtd->buff_ptr3 = cpu_to_le32(swap_temp + 0x3000);
	dtd->buff_ptr4 = cpu_to_le32(swap_temp + 0x4000);

	req->req.actual += length;
	*address = dtd;

	return length;
}

/*!
 * add USB request to dtd queue
 * @param req  USB request
 * @param dev  device pointer
 * @return Returns zero on success , or a negative error code 
 */
static int arcotg_req_to_dtd(struct arcotg_req *req, struct arcotg_udc *udc)
{
	unsigned max;
	unsigned count;
	int is_last;
	int is_first = 1;
	struct ep_td_struct *last_addr = NULL, *addr;

	pr_debug("udc req=0x%p\n", req);

	max = EP_MAX_LENGTH_TRANSFER;
	do {
		count = arcotg_build_dtd(req, max, &addr, udc);

		if (is_first) {
			is_first = 0;
			req->head = addr;
		} else {
			if (!last_addr) {
				/* FIXME last_addr not set.  iso only
				 * case, which we don't do yet
				 */
				pr_debug("udc wiping out something at 0!!\n");
			}

			last_addr->next_td_ptr = cpu_to_le32(addr->td_dma);
			last_addr->next_td_virt = addr;
			last_addr = addr;
		}

		/* last packet is usually short (or a zlp) */
		if (unlikely(count != max))
			is_last = 1;
		else if (likely(req->req.length != req->req.actual) ||
			 req->req.zero)
			is_last = 0;
		else
			is_last = 1;

		req->dtd_count++;
	} while (!is_last);

	addr->next_td_ptr = cpu_to_le32(DTD_NEXT_TERMINATE);
	addr->next_td_virt = NULL;
	req->tail = addr;

	return 0;
}

/*!
 * add transfer request to queue
 * @param  _ep endpoint pointer
 * @param _req request pointer
 * @param gfp_flags GFP_* flags to use
 * @return  Returns zero on success , or a negative error code
 */
static int arcotg_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			   gfp_t gfp_flags)
{
	struct arcotg_ep *ep = container_of(_ep, struct arcotg_ep, ep);
	struct arcotg_req *req = container_of(_req, struct arcotg_req, req);
	struct arcotg_udc *udc;
	unsigned long flags;
	int is_iso = 0;
	int rc = 0;

	pr_debug("udc _req=0x%p  len=%d\n", _req, _req->length);

	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
	    || !list_empty(&req->queue)) {
		pr_debug("udc %s, bad params\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!_ep || (!ep->desc && ep_index(ep))) {
		pr_debug("udc %s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}
	if (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
		is_iso = 1;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	req->ep = ep;

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif

	/* if data phase is absent send the status phase */
	if ((ep_index(ep) == 0)) {
		if (udc->ep0_state != DATA_STATE_XMIT &&
		    udc->ep0_state != DATA_STATE_RECV &&
		    (udc->local_setup_buff).wLength == 0) {
			if (ep0_prime_status(udc, EP_DIR_IN))
				ep0stall(udc);
			else
				return 0;
		}
	}

	/* map virtual address to hardware */
	if (req->req.dma == DMA_ADDR_INVALID) {
		req->req.dma = dma_map_single(ep->udc->gadget.dev.parent,
					      req->req.buf,
					      req->req.length, ep_is_in(ep)
					      ? DMA_TO_DEVICE :
					      DMA_FROM_DEVICE);
		req->mapped = 1;
		pr_debug("udc called dma_map_single(buffer,%s)  req=0x%p  "
			 "buf=0x%p  dma=0x%x  len=%d\n",
			 ep_is_in(ep) ? "to_dvc" : "from_dvc",
			 req, req->req.buf, req->req.dma, req->req.length);
		pr_debug("udc req=0x%p set req.dma=0x%x\n", req, req->req.dma);
	} else {
		dma_sync_single_for_device(ep->udc->gadget.dev.parent,
					   req->req.dma, req->req.length,
					   ep_is_in(ep) ? DMA_TO_DEVICE :
					   DMA_FROM_DEVICE);

		req->mapped = 0;
		pr_debug("udc called dma_sync_single_for_device(buffer,%s)  "
			 "req=0x%p  buf=0x%p  dma=0x%x  len=%d\n",
			 ep_is_in(ep) ? "to_dvc" : "from_dvc",
			 req, req->req.buf, req->req.dma, req->req.length);
	}

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->dtd_count = 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* push the dtds to device queue */
	if (!arcotg_req_to_dtd(req, udc))
		arcotg_queue_td(ep, req);
	else {
		rc = -ENOMEM;
		goto err;
	}

	/* EP0 */
	if ((ep_index(ep) == 0)) {
		udc->ep0_state = DATA_STATE_XMIT;
		pr_debug("udc ep0_state now DATA_STATE_XMIT\n");
	}

	/* put this req at the end of the ep's queue */
	/* irq handler advances the queue */
	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);

	dump_ep_queue(ep);
err:
	spin_unlock_irqrestore(&udc->lock, flags);

	return rc;
}

/*!
 * remove the endpoint buffer
 * @param _ep endpoint pointer
 * @param _req usb request pointer
 * @return Returns zero on success , or a negative error code
 */
static int arcotg_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct arcotg_ep *ep = container_of(_ep, struct arcotg_ep, ep);
	struct arcotg_req *req;
	unsigned long flags;

	pr_debug("%s\n", __FUNCTION__);
	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	}
	pr_debug("udc req=0x%p\n", req);

	done(ep, req, -ECONNRESET);

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;

}

/*-------------------------------------------------------------------------*/

/*!
 * modify the endpoint halt feature
 * @param  _ep  the non-isochronous endpoint being stalled
 * @param value 1--set halt  0--clear halt
 * @return Returns zero, or a negative error code
 */
static int _arcotg_ep_set_halt(struct usb_ep *_ep, int value)
{

	struct arcotg_ep *ep = container_of(_ep, struct arcotg_ep, ep);

	unsigned long flags = 0;
	int status = -EOPNOTSUPP;	/* operation not supported */
	unsigned char ep_dir = 0, ep_num = 0;
	struct arcotg_udc *udc;

	if (!_ep || !ep->desc) {
		status = -EINVAL;
		goto out;
	}

	udc = ep->udc;

	if (ep->desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		status = -EOPNOTSUPP;
		goto out;
	}

	/* Attemp to halt IN ep will fail if any transfer requests
	   are still queue */
	if (value && ep_is_in(ep) && !list_empty(&ep->queue)) {
		status = -EAGAIN;
		goto out;
	}

	status = 0;
	ep_dir = ep_is_in(ep) ? USB_SEND : USB_RECV;
	ep_num = (unsigned char)(ep_index(ep));
	spin_lock_irqsave(&ep->udc->lock, flags);
#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif
	dr_ep_change_stall(ep_num, ep_dir, value);
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	if (ep_index(ep) == 0) {
		udc->ep0_state = WAIT_FOR_SETUP;
		pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
		udc->ep0_dir = 0;
	}
      out:
	pr_debug("udc  %s %s halt rc=%d\n",
		 ep->ep.name, value ? "set" : "clear", status);

	return status;
}

static int arcotg_ep_set_halt(struct usb_ep *_ep, int value)
{
	return (_arcotg_ep_set_halt(_ep, value));
}

static int arcotg_fifo_status(struct usb_ep *_ep)
{
	struct arcotg_ep *ep;
	struct arcotg_udc *udc;
	int size = 0;
	u32 bitmask;
	struct ep_queue_head *d_qh;

	ep = container_of(_ep, struct arcotg_ep, ep);
	if (!_ep || (!ep->desc && ep_index(ep) != 0))
		return -ENODEV;

	udc = (struct arcotg_udc *)ep->udc;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	d_qh = &ep->udc->ep_qh[ep_index(ep) * 2 + ep_is_in(ep)];

	bitmask = (ep_is_in(ep)) ? (1 << (ep_index(ep) + 16)) :
	    (1 << (ep_index(ep)));

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif
	if (le32_to_cpu(usb_slave_regs->endptstatus) & bitmask)
		size = (d_qh->size_ioc_int_sts & DTD_PACKET_SIZE)
		    >> DTD_LENGTH_BIT_POS;

	pr_debug("%s %u\n", __FUNCTION__, size);
	return size;
}

static void arcotg_fifo_flush(struct usb_ep *_ep)
{
	struct arcotg_ep *ep;
	struct arcotg_udc *udc;
	u32 bitmask;
	int loops = 0;

	ep = container_of(_ep, struct arcotg_ep, ep);
	if (!_ep || (!ep->desc && ep_index(ep) != 0))
		return;

	udc = (struct arcotg_udc *)ep->udc;

	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return;

	bitmask = (ep_is_in(ep)) ? (1 << (ep_index(ep) + 16)) :
	    (1 << (ep_index(ep)));

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif
	do {
		/* set the flush bit, and wait for it to clear */
		usb_slave_regs->endptflush = cpu_to_le32(bitmask);
		while (usb_slave_regs->endptflush)
			continue;

		/* if ENDPTSTAT bit is set, the flush failed. Retry. */
		if (!(cpu_to_le32(usb_slave_regs->endptstatus) && bitmask))
			break;
	} while (++loops > 3);
}

/*!
 * endpoint callback functions
 */
static const struct usb_ep_ops arcotg_ep_ops = {
	.enable = arcotg_ep_enable,
	.disable = arcotg_ep_disable,

	.alloc_request = arcotg_alloc_request,
	.free_request = arcotg_free_request,

	.queue = arcotg_ep_queue,
	.dequeue = arcotg_ep_dequeue,

	.set_halt = arcotg_ep_set_halt,

	.fifo_status = arcotg_fifo_status,
	.fifo_flush = arcotg_fifo_flush,
};

/*-------------------------------------------------------------------------
		Gadget Driver Layer Operations
-------------------------------------------------------------------------*/

/*************************************************************************
		Gadget Driver Layer Operations
*************************************************************************/

/*!
 * Get the current frame number (from DR frame_index Reg )
 * @param gadget  gadget pointer
 * @return return frame count
 */
static int arcotg_get_frame(struct usb_gadget *gadget)
{
	struct arcotg_udc *udc = container_of(gadget, struct arcotg_udc, gadget);

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif
	return (int)(le32_to_cpu(usb_slave_regs->frindex) & USB_FRINDEX_MASKS);
}

/*-----------------------------------------------------------------------
 * Tries to wake up the host connected to this gadget
 *
 * Return : 0-success
 * Negative-this feature not enabled by host or not supported by device hw
 * FIXME: RM 16.6.2.2.1 DR support this wake-up feature?
 -----------------------------------------------------------------------*/
static int arcotg_wakeup(struct usb_gadget *gadget)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENOTSUPP;
}

/*! 
 * sets the device selfpowered feature
 * this affects the device status reported by the hw driver
 * to reflect that it now has a local power supply
 * usually device hw has register for this feature
 * @param gadget  gadget pointer
 * @param is_selfpowered self powered?
 * @return if selfpowered
 */
static int arcotg_set_selfpowered(struct usb_gadget *gadget, int is_selfpowered)
{
	pr_debug("%s\n", __FUNCTION__);
	return -ENOTSUPP;
}

static int can_pullup(struct arcotg_udc *udc)
{
	return udc->driver && udc->softconnect && udc->vbus_active;
}

/*!
 * Notify controller that VBUS is powered, Called by whatever 
 * detects VBUS sessions
 * @param gadger    gadger pointer
 * @param is_active is active?
 * @return Returns zero on success , or a negative error code
 */
static int arcotg_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct arcotg_udc *udc;
	unsigned long flags;

	pr_debug("%s\n", __FUNCTION__);

	udc = container_of(gadget, struct arcotg_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);
	pr_debug("udc VBUS %s\n", is_active ? "on" : "off");
	udc->vbus_active = (is_active != 0);

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static void do_charger_detect_work(struct work_struct *work)
{
	struct arcotg_udc *udc = 
		container_of(work, struct arcotg_udc, charger_detect_work.work);
	t_sensor_bits   sensors;

	pmic_get_sensors(&sensors);
	if (sensors.sense_usb4v4s ||
		(sensors.sense_chgcurrs && sensors.sense_chgdets)) {
			/* Do nothing */
	}
	else {
		UDC_LOG(ERROR, "ncf", "No charger found!\n");
		atomic_set(&udc->mA, 0);
		udc->driver->disconnect(&udc->gadget);
	}

	return;
}
 
/*! 
 * constrain controller's VBUS power usage
 * This call is used by gadget drivers during SET_CONFIGURATION calls,
 * reporting how much power the device may consume.  For example, this
 * could affect how quickly batteries are recharged.
 * Returns zero on success, else negative errno.
 * @param gadget    gadger pointer
 * @param mA	    power
 * @return Returns zero on success , or a negative error code
 */
static int arcotg_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct arcotg_udc *udc;

	pr_debug("%s(mA=%d)\n", __FUNCTION__, mA);
UDC_LOG(INFO, "mA", "(mA=%d)\n", mA);

	udc = container_of(gadget, struct arcotg_udc, gadget);

#ifdef CONFIG_MACH_LAB126
	atomic_set(&udc->mA, mA);
	atomic_set(&udc->run_workqueue, 0);
	udc->conn_sts = USB_CONN_HOST;
	charger_set_current_limit((int) atomic_read(&udc->mA));
	schedule_delayed_work(&udc->charger_detect_work, msecs_to_jiffies(1000));

	return 0;
#else
	if (udc->transceiver)
		return otg_set_power(udc->transceiver, mA);

	return -ENOTSUPP;
#endif
}

/*! 
 * Change Data+ pullup status
 * this func is used by usb_gadget_connect/disconnet
 * @param gadget    gadger pointer
 * @param is_on     on or off
 * @return Returns zero on success , or a negative error code
 */
static int arcotg_pullup(struct usb_gadget *gadget, int is_on)
{
	struct arcotg_udc *udc;

	pr_debug("%s\n", __FUNCTION__);

	udc = container_of(gadget, struct arcotg_udc, gadget);
	udc->softconnect = (is_on != 0);
#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif
	if (can_pullup(udc))
		usb_slave_regs->usbcmd |= USB_CMD_RUN_STOP;
	else
		usb_slave_regs->usbcmd &= ~USB_CMD_RUN_STOP;

	return 0;
}

static const struct usb_gadget_ops arcotg_gadget_ops = {
	.get_frame = arcotg_get_frame,
	.wakeup = arcotg_wakeup,
	.set_selfpowered = arcotg_set_selfpowered,
	.vbus_session = arcotg_vbus_session,
	.vbus_draw = arcotg_vbus_draw,
	.pullup = arcotg_pullup,
};

static void ep0stall(struct arcotg_udc *udc)
{
	u32 tmp;

	pr_debug("%s\n", __FUNCTION__);
	/* a protocol stall */
	tmp = le32_to_cpu(usb_slave_regs->endptctrl[0]);
	tmp |= EPCTRL_TX_EP_STALL | EPCTRL_RX_EP_STALL;
	usb_slave_regs->endptctrl[0] = cpu_to_le32(tmp);
	udc->ep0_state = WAIT_FOR_SETUP;
	pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
	udc->ep0_dir = 0;
}

/*! 
 * if direction is EP_IN, the status is Device->Host
 * if direction is EP_OUT, the status transaction is Device<-Host
 * @param udc device controller pointer
 * @param direction in or out
 * @return Returns zero on success , or a negative error code
 */
static int ep0_prime_status(struct arcotg_udc *udc, int direction)
{

	struct arcotg_req *req = udc->status_req;
	struct arcotg_ep *ep;
	int status = 0;
	unsigned long flags;

	pr_debug("%s\n", __FUNCTION__);
	if (direction == EP_DIR_IN)
		udc->ep0_dir = USB_DIR_IN;
	else
		udc->ep0_dir = USB_DIR_OUT;

	ep = &udc->eps[0];
	udc->ep0_state = WAIT_FOR_OUT_STATUS;
	pr_debug("udc ep0_state now WAIT_FOR_OUT_STATUS\n");

	req->ep = ep;
	req->req.length = 0;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;

	spin_lock_irqsave(&udc->lock, flags);

	if ((arcotg_req_to_dtd(req, udc) == 0))
		status = arcotg_queue_td(ep, req);
	else
		return -ENOMEM;

	if (status)
		UDC_LOG(ERROR, "sr1", "Can't get control status request \n");

	list_add_tail(&req->queue, &ep->queue);
	dump_ep_queue(ep);

	spin_unlock_irqrestore(&udc->lock, flags);

	return status;
}

static int udc_reset_ep_queue(struct arcotg_udc *udc, u8 pipe)
{
	struct arcotg_ep *ep = get_ep_by_pipe(udc, pipe);

	pr_debug("%s\n", __FUNCTION__);
	/* FIXME: collect completed requests? */
	if (!ep->name)
		return 0;

	nuke(ep, -ECONNRESET);

	return 0;
}
static void ch9setaddress(struct arcotg_udc *udc, u16 value, u16 index,
			  u16 length)
{
	pr_debug("udc new address=%d\n", value);

	/* Save the new address to device struct */
	udc->device_address = (u8) value;

	/* Update usb state */
	udc->usb_state = USB_STATE_ADDRESS;

	/* Status phase */
	if (ep0_prime_status(udc, EP_DIR_IN))
		ep0stall(udc);
}

static void ch9getstatus(struct arcotg_udc *udc, u8 request_type, u16 value,
			 u16 index, u16 length)
{
	u16 tmp = 0;		/* Status, cpu endian */

	struct arcotg_req *req;
	struct arcotg_ep *ep;
	int status = 0;

	ep = &udc->eps[0];

	if ((request_type & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		/* Get device status */
		tmp = 1 << USB_DEVICE_SELF_POWERED;
		tmp |= udc->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_INTERFACE) {
		/* Get interface status */
		/* We don't have interface information in udc driver */
		tmp = 0;
	} else if ((request_type & USB_RECIP_MASK) == USB_RECIP_ENDPOINT) {
		/* Get endpoint status */
		struct arcotg_ep *target_ep;

		target_ep = get_ep_by_pipe(udc, get_pipe_by_windex(index));

		/* stall if endpoint doesn't exist */
		if (!target_ep->desc)
			goto stall;
		tmp = dr_ep_get_stall(ep_index(target_ep), ep_is_in(target_ep))
				<< USB_ENDPOINT_HALT;
	}

	udc->ep0_dir = USB_DIR_IN;
	/* Borrow the per device status_req */
	req = udc->status_req;
	/* Fill in the reqest structure */
	*((u16 *) req->req.buf) = cpu_to_le16(tmp);
	req->ep = ep;
	req->req.length = 2;
	req->req.status = -EINPROGRESS;
	req->req.actual = 0;
	req->req.complete = NULL;
	req->dtd_count = 0;

	/* prime the data phase */
	if ((arcotg_req_to_dtd(req, udc) == 0))
		status = arcotg_queue_td(ep, req);
	else			/* no mem */
		goto stall;

	if (status) {
		UDC_LOG(ERROR, "sr2", "Can't respond to getstatus request\n");
		goto stall;
	}
	list_add_tail(&req->queue, &ep->queue);
	udc->ep0_state = DATA_STATE_XMIT;
	return;
stall:
	ep0stall(udc);
}

static void ch9setconfig(struct arcotg_udc *udc, u16 value, u16 index,
			 u16 length)
{
	pr_debug("udc 1 calling gadget driver->setup\n");
	udc->ep0_dir = USB_DIR_IN;
	if (udc->driver->setup(&udc->gadget, &udc->local_setup_buff) >= 0) {
		/* gadget layer deal with the status phase */
		udc->usb_state = USB_STATE_CONFIGURED;
		udc->ep0_state = WAIT_FOR_OUT_STATUS;
		pr_debug("udc ep0_state now WAIT_FOR_OUT_STATUS\n");
	}
}

static void setup_received_irq(struct arcotg_udc *udc,
			       struct usb_ctrlrequest *setup)
{
	u16 ptc = 0;		/* port test control */
	int handled = 1;	/* set to zero if we do not handle the message,
				   and should pass it to the gadget driver */

	pr_debug("udc request=0x%x\n", setup->bRequest);
	/* Fix Endian (udc->local_setup_buff is cpu Endian now) */
	setup->wValue = le16_to_cpu(setup->wValue);
	setup->wIndex = le16_to_cpu(setup->wIndex);
	setup->wLength = le16_to_cpu(setup->wLength);

	udc_reset_ep_queue(udc, 0);

	/* We asume setup only occurs on EP0 */
	if (setup->bRequestType & USB_DIR_IN)
		udc->ep0_dir = USB_DIR_IN;
	else
		udc->ep0_dir = USB_DIR_OUT;

	if ((setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
		/* handle class requests */
		switch (setup->bRequest) {

		case USB_BULK_RESET_REQUEST:
			udc->ep0_dir = USB_DIR_IN;
			if (udc->driver->setup(&udc->gadget,
					       &udc->local_setup_buff) >= 0) {
				udc->ep0_state = WAIT_FOR_SETUP;
				pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
			}
			break;

		default:
			handled = 0;
			break;
		}
	} else if ((setup->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		/* handle standard requests */
		switch (setup->bRequest) {

		case USB_REQ_GET_STATUS:
			if ((setup->
			     bRequestType & (USB_DIR_IN | USB_TYPE_STANDARD))
			    != (USB_DIR_IN | USB_TYPE_STANDARD))
				break;
			ch9getstatus(udc, setup->bRequestType, setup->wValue,
				     setup->wIndex, setup->wLength);
			break;

		case USB_REQ_SET_ADDRESS:
			if (setup->bRequestType !=
			    (USB_DIR_OUT | USB_TYPE_STANDARD |
			     USB_RECIP_DEVICE))
				break;
			ch9setaddress(udc, setup->wValue, setup->wIndex,
				      setup->wLength);
			break;

		case USB_REQ_SET_CONFIGURATION:
			if (setup->bRequestType !=
			    (USB_DIR_OUT | USB_TYPE_STANDARD |
			     USB_RECIP_DEVICE))
				break;
			/* gadget layer take over the status phase */
			ch9setconfig(udc, setup->wValue, setup->wIndex,
				     setup->wLength);
			break;
		case USB_REQ_SET_INTERFACE:
			if (setup->bRequestType !=
			    (USB_DIR_OUT | USB_TYPE_STANDARD |
			     USB_RECIP_INTERFACE))
				break;
			udc->ep0_dir = USB_DIR_IN;
			if (udc->driver->setup(&udc->gadget,
					       &udc->local_setup_buff) >= 0)
				/* gadget layer take over the status phase */
				break;
			/* Requests with no data phase */
		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			{	/* status transaction */
				int rc = -EOPNOTSUPP;

				if ((setup->bRequestType & USB_TYPE_MASK) !=
				    USB_TYPE_STANDARD)
					break;

				/* we only support set/clear feature for endpoint */
				if (setup->bRequestType == USB_RECIP_ENDPOINT) {
					int dir = (setup->wIndex & 0x0080) ?
					    EP_DIR_IN : EP_DIR_OUT;
					int num = (setup->wIndex & 0x000f);
					struct arcotg_ep *ep;

					if (setup->wValue != 0
					    || setup->wLength != 0
					    || (num * 2 + dir) > USB_MAX_PIPES)
						break;
					ep = &udc->eps[num * 2 + dir];

					if (setup->bRequest ==
					    USB_REQ_SET_FEATURE) {
						pr_debug("udc SET_FEATURE"
							 " doing set_halt\n");
						rc = _arcotg_ep_set_halt(&ep->
									 ep, 1);
					} else {
						pr_debug("udc CLEAR_FEATURE"
							 " doing clear_halt\n");
						rc = _arcotg_ep_set_halt(&ep->
									 ep, 0);
					}

				} else if (setup->bRequestType ==
					   USB_RECIP_DEVICE) {
					if (setup->bRequest ==
					    USB_REQ_SET_FEATURE) {
						ptc = setup->wIndex >> 8;
						rc = 0;
					}
					if (udc->gadget.is_otg) {
						if (setup->bRequest ==
							 USB_DEVICE_B_HNP_ENABLE)
							udc->gadget.b_hnp_enable = 1;
						else if (setup->bRequest ==
							 USB_DEVICE_A_HNP_SUPPORT)
							udc->gadget.a_hnp_support = 1;
						else if (setup->bRequest ==
							 USB_DEVICE_A_ALT_HNP_SUPPORT)
							udc->gadget.a_alt_hnp_support =
							    1;
						rc = 0;
					}
				}
				if (rc == 0) {
					/* send status only if _arcotg_ep_set_halt success */
					if (ep0_prime_status(udc, EP_DIR_IN))
						ep0stall(udc);
				}
				break;
			}
		default:
			handled = 0;
			break;
		}
	} else {
		/* vendor requests */
		handled = 0;
	}

	if (!handled) {
		if (udc->driver->setup(&udc->gadget, &udc->local_setup_buff)
		    != 0) {
			ep0stall(udc);
		} else if (setup->bRequestType & USB_DIR_IN) {
			udc->ep0_state = DATA_STATE_XMIT;
			pr_debug("udc ep0_state now DATA_STATE_XMIT\n");
		} else {
			if (setup->wLength != 0) {
				udc->ep0_state = DATA_STATE_RECV;
				pr_debug
				    ("udc ep0_state now DATA_STATE_RECV\n");
			}
		}
	}

	if (ptc) {
		usb_slave_regs->portsc1 |= ptc << 16;
		pr_debug("udc switch to test mode.\n");
	}
}

static void ep0_req_complete(struct arcotg_udc *udc, struct arcotg_ep *ep0,
			     struct arcotg_req *req)
{
	pr_debug("udc req=0x%p  ep0_state=0x%x\n", req, udc->ep0_state);
	if (udc->usb_state == USB_STATE_ADDRESS) {
		/* Set the new address */
		u32 new_address = (u32) udc->device_address;
		usb_slave_regs->deviceaddr = cpu_to_le32(new_address <<
							 USB_DEVICE_ADDRESS_BIT_POS);
		pr_debug("udc set deviceaddr to %d\n",
			 usb_slave_regs->
			 deviceaddr >> USB_DEVICE_ADDRESS_BIT_POS);
	}

	switch (udc->ep0_state) {
	case DATA_STATE_XMIT:

		done(ep0, req, 0);
		/* receive status phase */
		if (ep0_prime_status(udc, EP_DIR_OUT))
			ep0stall(udc);
		break;

	case DATA_STATE_RECV:

		done(ep0, req, 0);
		/* send status phase */
		if (ep0_prime_status(udc, EP_DIR_IN))
			ep0stall(udc);
		break;

	case WAIT_FOR_OUT_STATUS:
		done(ep0, req, 0);
		udc->ep0_state = WAIT_FOR_SETUP;
		pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
		break;

	case WAIT_FOR_SETUP:
		pr_debug("udc Unexpected interrupt\n");
		break;

	default:
		ep0stall(udc);
		break;
	}
}

static void tripwire_handler(struct arcotg_udc *udc, u8 ep_num, u8 * buffer_ptr)
{
	u32 temp;
	struct ep_queue_head *qh;

	qh = &udc->ep_qh[ep_num * 2 + EP_DIR_OUT];

	/* Clear bit in ENDPTSETUPSTAT */
	temp = cpu_to_le32(1 << ep_num);
	usb_slave_regs->endptsetupstat |= temp;

	/* while a hazard exists when setup package arrives */
	do {
		/* Set Setup Tripwire */
		temp = cpu_to_le32(USB_CMD_SUTW);
		usb_slave_regs->usbcmd |= temp;

		/* Copy the setup packet to local buffer */
		pr_debug("udc qh=0x%p  copy setup buffer from 0x%p to 0x%p\n",
			 qh, qh->setup_buffer, buffer_ptr);
		memcpy(buffer_ptr, (u8 *) qh->setup_buffer, 8);
	} while (!(le32_to_cpu(usb_slave_regs->usbcmd) & USB_CMD_SUTW));

	/* Clear Setup Tripwire */
	temp = le32_to_cpu(usb_slave_regs->usbcmd);
	temp &= ~USB_CMD_SUTW;
	usb_slave_regs->usbcmd = le32_to_cpu(temp);

	timeout = 10000000;
	while ((usb_slave_regs->endptsetupstat & 1) && --timeout) {
		continue;
	}
	if (timeout == 0) {
		UDC_LOG(ERROR, "to4", "TIMEOUT\n");
	}
}

/*!
 * process_ep_req(): free the completed Tds for this req 
 * FIXME: ERROR handling for multi-dtd requests 
 * @param udc  device controller pointer
 * @param pipe endpoint pipe
 * @param req  request pointer
 * @return Returns zero on success , or a negative error code
 */
static int process_ep_req(struct arcotg_udc *udc, int pipe,
			  struct arcotg_req *curr_req)
{
	struct ep_td_struct *curr_td, *tmp_td;
	int td_complete, actual, remaining_length, j, tmp;
	int status = 0;
	int errors = 0;
	struct ep_queue_head *curr_qh = &udc->ep_qh[pipe];
	int direction = pipe % 2;

	curr_td = curr_req->head;
	td_complete = 0;
	actual = curr_req->req.length;

	pr_debug
	    ("udc curr_req=0x%p  curr_td=0x%p  actual=%d  size_ioc_sts=0x%x\n",
	     curr_req, curr_td, actual, curr_td->size_ioc_sts);

	for (j = 0; j < curr_req->dtd_count; j++) {
		remaining_length = ((le32_to_cpu(curr_td->size_ioc_sts)
				     & DTD_PACKET_SIZE) >> DTD_LENGTH_BIT_POS);
		actual -= remaining_length;

		if ((errors = le32_to_cpu(curr_td->size_ioc_sts) &
		     DTD_ERROR_MASK)) {
			if (errors & DTD_STATUS_HALTED) {
				UDC_LOG(ERROR, "dtd", "dTD error %08x\n", errors);
				/* Clear the errors and Halt condition */
				tmp = le32_to_cpu(curr_qh->size_ioc_int_sts);
				tmp &= ~errors;
				curr_qh->size_ioc_int_sts = cpu_to_le32(tmp);
				status = -EPIPE;
				/*FIXME clearing active bit, update
				 * nextTD pointer re-prime ep */

				break;
			}
			if (errors & DTD_STATUS_DATA_BUFF_ERR) {
				pr_debug("udc Transfer overflow\n");
				status = -EPROTO;
				break;
			} else if (errors & DTD_STATUS_TRANSACTION_ERR) {
				pr_debug("udc ISO error\n");
				status = -EILSEQ;
				break;
			} else
				UDC_LOG(ERROR, "unk",
				       "Unknown error has occured (0x%x)!\n",
				       errors);

		} else if (le32_to_cpu(curr_td->size_ioc_sts) &
			   DTD_STATUS_ACTIVE) {
			pr_debug("udc Request not wholly complete dtd=0x%p\n",
				 curr_td);
			status = REQ_UNCOMPLETE;
			return status;
		} else if (remaining_length)
			if (direction) {
				pr_debug
				    ("udc Transmit dTD remaining length not zero "
				     "(rl=%d)\n", remaining_length);
				status = -EPROTO;
				break;
			} else {
				td_complete += 1;
				break;
		} else {
			td_complete += 1;
			pr_debug("udc dTD transmitted successful\n");
		}

		if (j != curr_req->dtd_count - 1)
			curr_td = curr_td->next_td_virt;
	}

	if (status)
		return status;

	curr_req->req.actual = actual;

	/* Free dtd for completed/error request */
	curr_td = curr_req->head;
	for (j = 0; j < curr_req->dtd_count; j++) {
		tmp_td = curr_td;
		if (j != curr_req->dtd_count - 1)
			curr_td = curr_td->next_td_virt;
		pr_debug("udc freeing dtd 0x%p  curr_req=0x%p\n", tmp_td,
			 curr_req);
		dma_pool_free(udc->dtd_pool, tmp_td, tmp_td->td_dma);
	}

	return status;
}

static void dtd_complete_irq(struct arcotg_udc *udc)
{
	u32 bit_pos;
	int i, ep_num, direction, bit_mask, status;
	struct arcotg_ep *curr_ep;
	struct arcotg_req *curr_req, *temp_req;

	pr_debug("%s\n", __FUNCTION__);
	/* Clear the bits in the register */
	bit_pos = usb_slave_regs->endptcomplete;
	usb_slave_regs->endptcomplete = bit_pos;
	bit_pos = le32_to_cpu(bit_pos);

	if (!bit_pos)
		return;

	for (i = 0; i < USB_MAX_ENDPOINTS * 2; i++) {
		ep_num = i >> 1;
		direction = i % 2;

		bit_mask = 1 << (ep_num + 16 * direction);

		if (!(bit_pos & bit_mask))
			continue;

		curr_ep = get_ep_by_pipe(udc, i);

		/* If the ep is configured */
		if (curr_ep->name == NULL) {
			UDC_LOG(WARN, "iep", "Invalid EP?\n");
			continue;
		}

		dump_ep_queue(curr_ep);

		/* search all arcotg_reqs of ep */
		list_for_each_entry_safe(curr_req, temp_req, &curr_ep->queue,
					 queue) {
			status = process_ep_req(udc, i, curr_req);
			if (status == REQ_UNCOMPLETE) {
				pr_debug
				    ("udc Not all tds are completed in the req\n");
				break;
			}

			if (ep_num == 0) {
				ep0_req_complete(udc, curr_ep, curr_req);
				break;
			} else
				done(curr_ep, curr_req, status);

		}

		dump_ep_queue(curr_ep);
	}
}

static void port_change_irq(struct arcotg_udc *udc)
{
	u32 speed;
	unsigned int portsc1;

	if (udc->bus_reset)
		udc->bus_reset = FALSE;

	/* Bus resetting is finished */
	if (!(le32_to_cpu(usb_slave_regs->portsc1) & PORTSCX_PORT_RESET)) {
		/* Get the speed */
		speed = (le32_to_cpu(usb_slave_regs->portsc1) &
			 PORTSCX_PORT_SPEED_MASK);
		switch (speed) {
		case PORTSCX_PORT_SPEED_HIGH:
			udc->gadget.speed = USB_SPEED_HIGH;
			break;
		case PORTSCX_PORT_SPEED_FULL:
			udc->gadget.speed = USB_SPEED_FULL;
			break;
		case PORTSCX_PORT_SPEED_LOW:
			udc->gadget.speed = USB_SPEED_LOW;
			break;
		default:
			udc->gadget.speed = USB_SPEED_UNKNOWN;
			break;
		}
	}

	portsc1 = le32_to_cpu(usb_slave_regs->portsc1);
	/* Check the High Speed Port settings */
	if ((portsc1 & PORTSCX_CURRENT_CONNECT_STATUS) &&
		(portsc1 & PORTSCX_PORT_HSP)) {
			udc->gadget.speed = USB_SPEED_HIGH;
	}

	UDC_LOG(INFO, "upc", "UDC port change, speed=%d\n", udc->gadget.speed);

	/* Update USB state */
	if (!udc->resume_state)
		udc->usb_state = USB_STATE_DEFAULT;
}

static void suspend_irq(struct arcotg_udc *udc)
{
	pr_debug("%s\n", __FUNCTION__);

	UDC_LOG(INFO, "usi", "%s: usb_slave_regs->usbintr:%x\n", __FUNCTION__,
			usb_slave_regs->usbintr);

	udc->resume_state = udc->usb_state;
	udc->usb_state = USB_STATE_SUSPENDED;
	
	/* report suspend to the driver ,serial.c not support this */
	if (udc->driver && udc->driver->suspend)
		udc->driver->suspend(&udc->gadget);
}

static void resume_irq(struct arcotg_udc *udc)
{
	pr_debug("%s\n", __FUNCTION__);

	UDC_LOG(INFO, "uri", "%s: usb_slave_regs->usbintr:%x\n", __FUNCTION__,
                        usb_slave_regs->usbintr);

	udc->usb_state = udc->resume_state;
	udc->resume_state = 0;

	/* report resume to the driver , serial.c not support this */
	if (udc->driver && udc->driver->resume)
		udc->driver->resume(&udc->gadget);

}

/* 
 * If we get here, the Host enumeration has failed. To recover
 * the PHY, we need to do a full udc suspend/resume
 */
static void udc_full_suspend_resume(struct arcotg_udc *udc)
{
	disable_irq(PMIC_IRQ);
	arcotg_full_suspend(udc);
	mdelay(500); /* Settling time */
	arcotg_full_resume(udc);
	enable_irq(PMIC_IRQ);
}

static inline void reset_queues(struct arcotg_udc *udc)
{
	u8 pipe;

	pr_debug("udc disconnect\n");
	for (pipe = 0; pipe < udc->max_pipes; pipe++)
		udc_reset_ep_queue(udc, pipe);

	atomic_inc(&port_reset_counter);

	/* report disconnect; the driver is already quiesced */
	if (udc->driver)
		udc->driver->disconnect(&udc->gadget);

	if (atomic_read(&port_reset_counter) == 3 && (!udc_full_sr)) {
		UDC_LOG(CRIT, "hmf", "USB Host mode failed enumeration\n");
		/* Host mode failed to enumerate, do full suspend/resume */
		udc_full_suspend_resume(udc);
	}
}

static void reset_irq(struct arcotg_udc *udc)
{
	u32 temp;

	/* Clear the device address */
	temp = le32_to_cpu(usb_slave_regs->deviceaddr);
	temp &= ~USB_DEVICE_ADDRESS_MASK;
	usb_slave_regs->deviceaddr = cpu_to_le32(temp);
	pr_debug("udc set deviceaddr to %d\n",
		 usb_slave_regs->deviceaddr >> USB_DEVICE_ADDRESS_BIT_POS);
	udc->device_address = 0;

	/* Clear usb state */
	udc->usb_state = USB_STATE_DEFAULT;
	udc->remote_wakeup = 0;	/* default to 0 on reset */

	/* Clear all the setup token semaphores */
	temp = le32_to_cpu(usb_slave_regs->endptsetupstat);
	usb_slave_regs->endptsetupstat = cpu_to_le32(temp);

	/* Clear all the endpoint complete status bits */
	temp = le32_to_cpu(usb_slave_regs->endptcomplete);
	usb_slave_regs->endptcomplete = cpu_to_le32(temp);

	timeout = 10000000;
	/* Wait until all endptprime bits cleared */
	while ((usb_slave_regs->endpointprime) && --timeout) {
		continue;
	}
	if (timeout == 0) {
		UDC_LOG(ERROR, "to5", "TIMEOUT\n");
	}

	/* Write 1s to the Flush register */
	usb_slave_regs->endptflush = 0xFFFFFFFF;

	if (le32_to_cpu(usb_slave_regs->portsc1) & PORTSCX_PORT_RESET) {
		UDC_LOG(INFO, "ubr", "UDC Bus Reset\n");
		/* Bus is reseting */
		udc->bus_reset = TRUE;
		udc->ep0_state = WAIT_FOR_SETUP;
		pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
		udc->ep0_dir = 0;
		/* Reset all the queues, include XD, dTD, EP queue
		 * head and TR Queue */
		reset_queues(udc);
	} else {
		pr_debug("udc Controller reset\n");
		/* initialize usb hw reg except for regs for EP, not
		 * touch usbintr reg */
		dr_controller_setup(udc);

		/* FIXME: Reset all internal used Queues */
		reset_queues(udc);

		ep0_dr_and_qh_setup(udc);

		/* Enable DR IRQ reg, Set Run bit, change udc state */
		dr_controller_run(udc);
		udc->usb_state = USB_STATE_ATTACHED;
		udc->ep0_state = WAIT_FOR_SETUP;
		pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
		udc->ep0_dir = 0;
	}
}

#ifdef CONFIG_MACH_LAB126
static void delete_charger_timer(struct arcotg_udc *udc)
{
	del_timer(&udc->charger_timer);
	atomic_set(&udc->timer_scheduled, 0);
	charger_misdetect_retry = 0;	// allow charger readings to continue

	atomic_set(&arcotg_busy, 0);

	/* charger timer has been deleted */
	udc_charger_timer_deleted = 1;
}
#endif

static irqreturn_t arcotg_udc_irq(int irq, void *_udc)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)_udc;
	u32 irq_src;
	irqreturn_t status = IRQ_NONE;
	unsigned long flags;

	if (udc->stopped)
		return IRQ_NONE;	/* ignore irq if we're not running */

	spin_lock_irqsave(&udc->lock, flags);
	irq_src = usb_slave_regs->usbsts & usb_slave_regs->usbintr;
	pr_debug("udc irq_src [0x%08x]\n", irq_src);
	/* Clear notification bits */
	usb_slave_regs->usbsts &= irq_src;
	irq_src = le32_to_cpu(irq_src);

	/* USB Interrupt */
	if (irq_src & USB_STS_INT) {
		/* Setup packet, we only support ep0 as control ep */
		pr_debug("udc endptsetupstat=0x%x  endptcomplete=0x%x\n",
			 usb_slave_regs->endptsetupstat,
			 usb_slave_regs->endptcomplete);
		atomic_set(&port_reset_counter, 0);
		if (usb_slave_regs->
		    endptsetupstat & cpu_to_le32(EP_SETUP_STATUS_EP0)) {
			tripwire_handler(udc, 0,
					 (u8 *) (&udc->local_setup_buff));
			setup_received_irq(udc, &udc->local_setup_buff);
			status = IRQ_HANDLED;
			delete_charger_timer(udc);
		}

		/* completion of dtd */
		if (usb_slave_regs->endptcomplete) {
			dtd_complete_irq(udc);
			status = IRQ_HANDLED;
			delete_charger_timer(udc);
		}
	}

	/* SOF (for ISO transfer) */
	if (irq_src & USB_STS_SOF) {
		status = IRQ_HANDLED;
		delete_charger_timer(udc);
	}

	/* Port Change */
	if (irq_src & USB_STS_PORT_CHANGE) {	
		port_change_irq(udc);
		status = IRQ_HANDLED;
		delete_charger_timer(udc);
	}

	/* Reset Received */
	if (irq_src & USB_STS_RESET) {
		reset_irq(udc);
		status = IRQ_HANDLED;
		delete_charger_timer(udc);
	}

	/* Sleep Enable (Suspend) */
	if (irq_src & USB_STS_SUSPEND) {
		suspend_irq(udc);
		status = IRQ_HANDLED;
	} else if (udc->resume_state) {
		resume_irq(udc);
		status = IRQ_HANDLED;
		delete_charger_timer(udc);
	}

	if (irq_src & (USB_STS_ERR | USB_STS_SYS_ERR)) {
		pr_debug("udc Error IRQ %x\n", irq_src);
		/*
		 * Error IRQ - reset the port
		 */
		reset_irq(udc);
		status = IRQ_HANDLED;
	}

	if (status != IRQ_HANDLED) {
		pr_debug("udc not handled  irq_src=0x%x\n", irq_src);
	}

	pr_debug("udc irq_src [0x%08x] done.  regs now=0x%08x\n", irq_src,
		 usb_slave_regs->usbsts & usb_slave_regs->usbintr);
	pr_debug("-\n");
	pr_debug("-\n");
	spin_unlock_irqrestore(&udc->lock, flags);

	return status;
}

/*!
 * tell the controller driver about gadget layer driver
 * The driver's bind function will be called to bind it to a gadget.
 * @param driver  for example fsg_driver from file_storage.c
 * @return Returns zero on success , or a negative error code
 */
int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	int retval = -ENODEV;
	unsigned long flags = 0;
	struct arcotg_udc *udc = udc_controller;

	pr_debug("udc udc=0x%p\n", udc);

	/* standard operations */
	if (!udc)
		return -ENODEV;

	if (!driver || (driver->speed != USB_SPEED_FULL
			&& driver->speed != USB_SPEED_HIGH)
	    		|| !driver->bind || !driver->unbind ||
	    		!driver->disconnect || !driver->setup)
		return -EINVAL;

	if (udc->driver)
		return -EBUSY;

	arcotg_reschedule_work(udc);

	/* lock is needed but whether should use this lock or another */
	spin_lock_irqsave(&udc->lock, flags);

	driver->driver.bus = 0;
	/* hook up the driver */
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	spin_unlock_irqrestore(&udc->lock, flags);

	retval = driver->bind(&udc->gadget);
	if (retval) {
		pr_debug("bind to %s --> %d\n", driver->driver.name, retval);
		udc->gadget.dev.driver = 0;
		udc->driver = 0;
		goto out;
	}

	if (udc->transceiver) {
		/* Suspend the controller until OTG enables it */
		udc_suspend(udc);
		pr_debug("udc suspend udc for OTG auto detect \n");

		/* Export udc suspend/resume call to OTG */
		udc->gadget.dev.parent->driver->suspend = fsl_udc_suspend;
		udc->gadget.dev.parent->driver->resume = fsl_udc_resume;

		/* connect to bus through transceiver */
		retval = otg_set_peripheral(udc->transceiver, &udc->gadget);
		if (retval < 0) {
			pr_debug("udc can't bind to transceiver\n");
			driver->unbind(&udc->gadget);
			udc->gadget.dev.driver = 0;
			udc->driver = 0;
			return retval;
		}
	} else {
		/* Enable DR IRQ reg and Set usbcmd reg  Run bit */
		dr_controller_run(udc);
		udc->usb_state = USB_STATE_ATTACHED;
		udc->ep0_state = WAIT_FOR_SETUP;
		pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
		udc->ep0_dir = 0;
	}

	UDC_LOG(INFO, "bnd", "gadget %s bound to driver %s\n",
	       udc->gadget.name, driver->driver.name);

      out:
	return retval;
}

EXPORT_SYMBOL(usb_gadget_register_driver);

/* Disconnect from gadget driver */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct arcotg_ep *loop_ep;
	unsigned long flags;
	struct arcotg_udc *udc = udc_controller;

	pr_debug("usb_gadget_unregister_driver udc=0x%p\n", udc);
	if (!udc)
		return -ENODEV;

	if (!driver || driver != udc->driver)
		return -EINVAL;

#ifdef CONFIG_MACH_LAB126
	arcotg_reschedule_work(udc);
#endif

	if (udc->transceiver) {
		(void)otg_set_peripheral(udc->transceiver, 0);
		pr_debug("udc set peripheral=NULL\n");
	} else {
		/* FIXME
		   pullup_disable(udc);
		 */
	}

	/* stop DR, disable intr */
	dr_controller_stop(udc);

	/* in fact, no needed */
	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	pr_debug("udc ep0_state now WAIT_FOR_SETUP\n");
	udc->ep0_dir = 0;

	/* stand operation */
	spin_lock_irqsave(&udc->lock, flags);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	nuke(&udc->eps[0], -ESHUTDOWN);
	list_for_each_entry(loop_ep, &udc->gadget.ep_list, ep.ep_list)
	    nuke(loop_ep, -ESHUTDOWN);

	/* report disconnect to free up endpoints */
	pr_debug("udc disconnect\n");
	driver->disconnect(&udc->gadget);

	spin_unlock_irqrestore(&udc->lock, flags);

	/* unbind gadget and unhook driver. */
	pr_debug("udc unbind\n");
	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = 0;
	udc->driver = 0;

	UDC_LOG(INFO, "unrg", "unregistered gadget driver '%s'\n",
	       driver->driver.name);
	return 0;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);

/*-------------------------------------------------------------------------
		PROC File System Support
-------------------------------------------------------------------------*/
#ifdef CONFIG_USB_GADGET_DEBUG_FILES

#include <linux/seq_file.h>

static const char proc_filename[] = "driver/arcotg_udc";

static int fsl_proc_read(char *page, char **start, off_t off, int count,
			    int *eof, void *_dev)
{
	char *buf = page;
	char *next = buf;
	unsigned size = count;
	unsigned long flags;
	int t, i;
	u32 tmp_reg;
	struct arcotg_ep *ep;
	struct arcotg_req *req;

	struct arcotg_udc *udc = udc_controller;
	if (off != 0)
		return 0;

	spin_lock_irqsave(&udc->lock, flags);

	/* ------basic driver infomation ---- */
	t = scnprintf(next, size,
		      DRIVER_DESC "\n" "%s version %s\n"
		      "Gadget driver %s\n\n", driver_name, DRIVER_VERSION,
		      udc->driver ? udc->driver->driver.name : "(none)");
	size -= t;
	next += t;

	/* ------ DR Registers ----- */
	tmp_reg = le32_to_cpu(usb_slave_regs->usbcmd);
	t = scnprintf(next, size,
		      "USBCMD reg\n" "SetupTW %d\n" "Run/Stop %s\n\n",
		      (tmp_reg & USB_CMD_SUTW) ? 1 : 0,
		      (tmp_reg & USB_CMD_RUN_STOP) ? "Run" : "Stop");
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->usbsts);
	t = scnprintf(next, size,
		      "USB Status Reg\n" "Dr Suspend %d "
		      "Reset Received %d " "System Error %s "
		      "USB Error Interrupt %s\n\n",
		      (tmp_reg & USB_STS_SUSPEND) ? 1 : 0,
		      (tmp_reg & USB_STS_RESET) ? 1 : 0,
		      (tmp_reg & USB_STS_SYS_ERR) ? "Err" : "Normal",
		      (tmp_reg & USB_STS_ERR) ? "Err detected" : "No err");
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->usbintr);
	t = scnprintf(next, size,
		      "USB Intrrupt Enable Reg\n"
		      "Sleep Enable %d " "SOF Received Enable %d "
		      "Reset Enable %d\n" "System Error Enable %d "
		      "Port Change Dectected Enable %d\n"
		      "USB Error Intr Enable %d "
		      "USB Intr Enable %d\n\n",
		      (tmp_reg & USB_INTR_DEVICE_SUSPEND) ? 1 : 0,
		      (tmp_reg & USB_INTR_SOF_EN) ? 1 : 0,
		      (tmp_reg & USB_INTR_RESET_EN) ? 1 : 0,
		      (tmp_reg & USB_INTR_SYS_ERR_EN) ? 1 : 0,
		      (tmp_reg & USB_INTR_PTC_DETECT_EN) ? 1 : 0,
		      (tmp_reg & USB_INTR_ERR_INT_EN) ? 1 : 0,
		      (tmp_reg & USB_INTR_INT_EN) ? 1 : 0);
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->frindex);
	t = scnprintf(next, size,
		      "USB Frame Index Reg " "Frame Number is 0x%x\n\n",
		      (tmp_reg & USB_FRINDEX_MASKS));
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->deviceaddr);
	t = scnprintf(next, size,
		      "USB Device Address Reg " "Device Addr is 0x%x\n\n",
		      (tmp_reg & USB_DEVICE_ADDRESS_MASK));
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->endpointlistaddr);
	t = scnprintf(next, size,
		      "USB Endpoint List Address Reg "
		      "Device Addr is 0x%x\n\n",
		      (tmp_reg & USB_EP_LIST_ADDRESS_MASK));
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->portsc1);
	t = scnprintf(next, size,
		      "USB Port Status&Control Reg\n"
		      "Port Transceiver Type %s " "Port Speed %s \n"
		      "PHY Low Power Suspend %s " "Port Reset %s "
		      "Port Suspend Mode %s \n" "Over-current Change %s "
		      "Port Enable/Disable Change %s\n"
		      "Port Enabled/Disabled %s "
		      "Current Connect Status %s\n\n",
			( {
				char *s;
				switch (tmp_reg & PORTSCX_PTS_FSLS) {
				case PORTSCX_PTS_UTMI:
					s = "UTMI"; break; 
				case PORTSCX_PTS_ULPI:
					s = "ULPI "; break; 
				case PORTSCX_PTS_FSLS:
					s = "FS/LS Serial"; break; 
				default:
					s = "None"; break;}
				s;} ),
			( {
				char *s;
				switch (tmp_reg & PORTSCX_PORT_SPEED_UNDEF) {
				case PORTSCX_PORT_SPEED_FULL:
					s = "Full Speed"; break;
				case PORTSCX_PORT_SPEED_LOW:
					s = "Low Speed"; break;
				case PORTSCX_PORT_SPEED_HIGH:
					s = "High Speed"; break;
				default:
					s = "Undefined"; break;}
				s;} ),
			(tmp_reg & PORTSCX_PHY_LOW_POWER_SPD) ?
				"Normal PHY mode" : "Low power mode",
			(tmp_reg & PORTSCX_PORT_RESET) ? "In Reset" :
				"Not in Reset",
			(tmp_reg & PORTSCX_PORT_SUSPEND) ? "In " : "Not in",
			(tmp_reg & PORTSCX_OVER_CURRENT_CHG) ? "Dected" : "No",
			(tmp_reg & PORTSCX_PORT_EN_DIS_CHANGE) ? "Disable" :
				"Not change",
			(tmp_reg & PORTSCX_PORT_ENABLE) ? "Enable" :
				"Not correct",
			(tmp_reg & PORTSCX_CURRENT_CONNECT_STATUS) ?
				"Attached" : "Not-Att") ;
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->usbmode);
	t = scnprintf(next, size, "USB Mode Reg " "Controller Mode is  %s\n\n",
			( {
				char *s;
				switch (tmp_reg & USB_MODE_CTRL_MODE_HOST) {
				case USB_MODE_CTRL_MODE_IDLE:
					s = "Idle"; break;
				case USB_MODE_CTRL_MODE_DEVICE:
					s = "Device Controller"; break;
				case USB_MODE_CTRL_MODE_HOST:
					s = "Host Controller"; break;
				default:
					s = "None"; break;}
				s;}
			)) ;
	size -= t;
	next += t;

	tmp_reg = le32_to_cpu(usb_slave_regs->endptsetupstat);
	t = scnprintf(next, size,
		      "Endpoint Setup Status Reg" "SETUP on ep 0x%x\n\n",
		      (tmp_reg & EP_SETUP_STATUS_MASK));
	size -= t;
	next += t;

	for (i = 0; i < USB_MAX_ENDPOINTS; i++) {
		tmp_reg = le32_to_cpu(usb_slave_regs->endptctrl[i]);
		t = scnprintf(next, size, "EP Ctrl Reg [0x%x] = [0x%x]\n",
			      i, tmp_reg);
		size -= t;
		next += t;
	}
	tmp_reg = le32_to_cpu(usb_slave_regs->endpointprime);
	t = scnprintf(next, size, "EP Prime Reg = [0x%x]\n", tmp_reg);
	size -= t;
	next += t;

	/* ------arcotg_udc, arcotg_ep, arcotg_request structure information ----- */
	ep = &udc->eps[0];
	t = scnprintf(next, size, "For %s Maxpkt is 0x%x index is 0x%x\n",
		      ep->ep.name, ep_maxpacket(ep), ep_index(ep));
	size -= t;
	next += t;

	if (list_empty(&ep->queue)) {
		t = scnprintf(next, size, "its req queue is empty\n\n");
		size -= t;
		next += t;
	} else {
		list_for_each_entry(req, &ep->queue, queue) {
			t = scnprintf(next, size,
				      "req %p actual 0x%x length 0x%x  buf %p\n",
				      &req->req, req->req.actual,
				      req->req.length, req->req.buf);
			size -= t;
			next += t;
		}
	}
	/* other gadget->eplist ep */
	list_for_each_entry(ep, &udc->gadget.ep_list, ep.ep_list) {
		if (ep->desc) {
			t = scnprintf(next, size,
				      "\nFor %s Maxpkt is 0x%x index is 0x%x\n",
				      ep->ep.name,
				      ep_maxpacket(ep), ep_index(ep));
			size -= t;
			next += t;

			if (list_empty(&ep->queue)) {
				t = scnprintf(next, size,
					      "its req queue is empty\n\n");
				size -= t;
				next += t;
			} else {
				list_for_each_entry(req, &ep->queue, queue) {
					t = scnprintf(next, size,
						      "req %p actual 0x%x length"
						      " 0x%x  buf %p\n",
						      &req->req,
						      req->req.actual,
						      req->req.length,
						      req->req.buf);
					size -= t;
					next += t;
				}
			}
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	*eof = 1;
	return count - size;
}

#define create_proc_file()	create_proc_read_entry(proc_filename, \
				0, NULL, fsl_proc_read, NULL)

#define remove_proc_file()	remove_proc_entry(proc_filename, NULL)

#else				/* !CONFIG_USB_GADGET_DEBUG_FILES */

#define create_proc_file()	do {} while (0)
#define remove_proc_file()	do {} while (0)

#endif				/*CONFIG_USB_GADGET_DEBUG_FILES */

/*-------------------------------------------------------------------------*/

/*!
 * Release the ARC OTG specific udc structure
 * it is not stand gadget function
 * it is called when the last reference to the device is removed;
 * it is called from the embedded kobject's release method.
 * All device structures registered with the core must have a
 * release method, or the kernel prints out scary complaints
 * @param dev device controller pointer
 */
static void fsl_udc_release(struct device *dev)
{
	struct device *udc_dev = dev->parent;

	struct arcotg_udc *udc = (struct arcotg_udc *)dev_get_drvdata(udc_dev);

	complete(udc->done);
	dma_free_coherent(dev, udc->ep_qh_size, udc->ep_qh, udc->ep_qh_dma);
	kfree(udc);
}

/******************************************************************
	Internal Structure Build up functions -2
*******************************************************************/

static ssize_t
show_connected(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t
show_phy_manufacturer(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", local_phy_ops->name);
}

static ssize_t
show_log_mask(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%08x\n", g_lab126_log_mask);
}


static ssize_t
store_log_mask(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int		i;

	if (sscanf(buf, "%x", &i) != 1)
		return -EINVAL;

	g_lab126_log_mask = i;
	return count;
}

/*!
 * this func will init resource for globle controller
 * Return the udc handle on success or Null on failing
 * @param pdev device controller pointer
 */
static void *struct_udc_setup(struct platform_device *pdev)
{
	struct arcotg_udc *udc;
	struct usb_request *req;

	udc = (struct arcotg_udc *)
	    kmalloc(sizeof(struct arcotg_udc), GFP_KERNEL);
	pr_debug("udc kmalloc(ucd)=0x%p\n", udc);
	if (udc == NULL) {
		UDC_LOG(ERROR, "kmu", "malloc udc failed\n");
		goto err1;
	}

	/* Zero out the internal USB state structure */
	memset(udc, 0, sizeof(struct arcotg_udc));

	/* initialized QHs, take care the 2K align */
	udc->ep_qh_size = USB_MAX_PIPES * sizeof(struct ep_queue_head);

	/* Arc OTG IP-core requires 2K alignment of queuehead
	 * this if fullfilled by per page allocation
	 * by dma_alloc_coherent(...)
	 */
	udc->ep_qh = (struct ep_queue_head *)
	    dma_alloc_coherent(&pdev->dev, udc->ep_qh_size,
			       &udc->ep_qh_dma, GFP_ATOMIC);
	if (!udc->ep_qh) {
		UDC_LOG(ERROR, "kmq", "malloc QHs for udc failed\n");
		goto err2;
	}
	pr_debug("udc udc->ep_qh=0x%p\n", udc->ep_qh);

	memset(udc->ep_qh, 0, udc->ep_qh_size);

	/* need 32 byte alignment, don't cross 4K boundary */
	udc->dtd_pool = dma_pool_create("arcotg_dtd", &pdev->dev,
					sizeof(struct ep_td_struct), 32, 8192);
	if (!udc->dtd_pool) {
		UDC_LOG(ERROR, "dpa", "dtd_pool alloc failed\n");
		goto err3;
	}

	/* Initialize ep0 status request structure */
	/* FIXME: arcotg_alloc_request() ignores ep argument */
	req = arcotg_alloc_request_local(GFP_KERNEL);
	if (req == NULL) {
		UDC_LOG(ERROR, "aar", "arcotg_alloc_request failed\n");
		goto err4;
	}
	udc->status_req = container_of(req, struct arcotg_req, req);

	/* allocate a small amount of memory to get valid address */
	udc->status_req->req.buf = kmalloc(8, GFP_KERNEL);
	if (udc->status_req->req.buf == NULL) {
		UDC_LOG(ERROR, "kmb", "kmalloc failed\n");
		goto err5;
	}
	udc->status_req->req.dma = virt_to_phys(udc->status_req->req.buf);

	pr_debug("udc status_req=0x%p  status_req->req.buf=0x%p  "
		 "status_req->req.dma=0x%x",
		 udc->status_req, udc->status_req->req.buf,
		 udc->status_req->req.dma);

	udc->resume_state = USB_STATE_NOTATTACHED;
	udc->usb_state = USB_STATE_POWERED;
	udc->ep0_dir = 0;
	udc->remote_wakeup = 0;	/* default to 0 on reset */
	/* initliaze the arcotg_udc lock */
	spin_lock_init(&udc->lock);

	return udc;

err5:
	arcotg_free_request(NULL, req);
err4:
	dma_pool_destroy(udc->dtd_pool);
err3:
	dma_free_coherent(&pdev->dev, udc->ep_qh_size, udc->ep_qh,
			udc->ep_qh_dma);
err2:
	kfree(udc);
err1:
	return NULL;
}

/*!
 * set up the arcotg_ep struct for eps
 * ep0out isnot used so do nothing here
 * ep0in should be taken care
 * It also link this arcotg_ep->ep to gadget->ep_list
 * @param udc   device controller pointer 
 * @param pipe_num pipe number
 * @return Returns zero on success , or a negative error code
 */
static int struct_ep_setup(struct arcotg_udc *udc, unsigned char pipe_num)
{
	struct arcotg_ep *ep = get_ep_by_pipe(udc, pipe_num);

	/*
	   VDBG("pipe_num=%d  name[%d]=%s",
	   pipe_num, pipe_num, ep_name[pipe_num]);
	 */
	ep->udc = udc;
	strcpy(ep->name, ep_name[pipe_num]);
	ep->ep.name = ep_name[pipe_num];
	ep->ep.ops = &arcotg_ep_ops;
	ep->stopped = 0;

	/* for ep0: the desc defined here;
	 * for other eps, gadget layer called ep_enable with defined desc
	 */
	/* for ep0: maxP defined in desc
	 * for other eps, maxP is set by epautoconfig() called by gadget layer
	 */
	if (pipe_num == 0) {
		ep->desc = &arcotg_ep0_desc;
		ep->ep.maxpacket = USB_MAX_CTRL_PAYLOAD;
	} else {
		ep->ep.maxpacket = (unsigned short)~0;
		ep->desc = NULL;
	}

	/* the queue lists any req for this ep */
	INIT_LIST_HEAD(&ep->queue);

	/* arcotg_ep->ep.ep_list: gadget ep_list hold all of its eps
	 * so only the first should init--it is ep0' */

	/* gagdet.ep_list used for ep_autoconfig so no ep0 */
	if (pipe_num != 0)
		list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);

	ep->gadget = &udc->gadget;

	return 0;
}

static int board_init(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata;
	pdata = (struct fsl_usb2_platform_data *)pdev->dev.platform_data;

	pr_debug("%s pdev=0x%p  pdata=0x%p\n", __FUNCTION__, pdev, pdata);

	/*
	 * do platform specific init: check the clock, grab/config pins, etc.
	 */
	if (pdata->platform_init && pdata->platform_init(pdev) != 0)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_MACH_LAB126

static inline void suspend_port(void)
{
	usb_slave_regs->portsc1 |= cpu_to_le32(PORTSCX_PHY_LOW_POWER_SPD);
}

static inline void suspend_ctrl(void)
{
	unsigned int tmp;
	
	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp &= ~USB_CMD_RUN_STOP;
	usb_slave_regs->usbcmd = cpu_to_le32(tmp);
}

static inline void resume_port(void)
{
	if (NXP_ID == local_phy_ops->id) {
		//Enable phy ULPI interface
		mxc_set_gpio_dataout(MX31_PIN_GPIO1_6, 0);
		mdelay(2);
	}

	// Resume the PHY
	usb_slave_regs->portsc1 &= cpu_to_le32(~PORTSCX_PHY_LOW_POWER_SPD);
	mdelay(2);

	usb_slave_regs->portsc1 &= cpu_to_le32(~PORTSCX_PORT_SUSPEND);
	usb_slave_regs->portsc1 |= cpu_to_le32(PORTSCX_PORT_RESET);
	mdelay(3);
}

static inline void resume_ctrl(void)
{
	unsigned int tmp;

	tmp = le32_to_cpu(usb_slave_regs->usbcmd);
	tmp |= USB_CMD_RUN_STOP;
	usb_slave_regs->usbcmd = cpu_to_le32(tmp);
}

static void fsl_udc_low_power_suspend(struct arcotg_udc *udc)
{
	int timeout = 10;
	unsigned long flags;

	if (recovery_mode)
		return;

	if (udc->suspended) {	
		return;
	}
	disable_irq(ARCOTG_IRQ);
	udc->suspended = 1;
	suspend_port();
	mdelay(20);
	suspend_ctrl();
	mdelay(20);
	suspend_irq(udc);

	/* Check if CPU is scaling */
	while (atomic_read(&usb_clk_change) && (timeout != 0)) {
		schedule();
		timeout--;	
	}

	/* Make sure the clock gating does not get interrupted */
	spin_lock_irqsave(&udc->lock, flags);
	clk_disable(usb_ahb_clk);
	spin_unlock_irqrestore(&udc->lock, flags);

	/* Get into low power IDLE */
	atomic_set(&usb_dma_doze_ref_count, 0);
	udc->stopped = 1;
}

static void reset_nxp_phy(void)
{
	u32 portsc;

	//Enable phy ULPI interface
	mxc_set_gpio_dataout(MX31_PIN_GPIO1_6, 0);
	mdelay(250);

	//Set the Mux to ULPI
	portsc = le32_to_cpu(usb_slave_regs->portsc1);
	portsc &= ~PORTSCX_PHY_TYPE_SEL;
	portsc |= PORTSCX_PTS_ULPI;
	usb_slave_regs->portsc1 = cpu_to_le32(portsc);
	udelay(250);

	//Reset the phy
	isp1504_set(0x20, 0x05, &(usb_slave_regs->ulpiview));

	//Wait for the PHY to come back up
	mdelay(3);
}

static void reset_smsc_phy(void)
{
	mxc_set_gpio_dataout(MX31_PIN_GPIO3_0, 0);
	mdelay(1);
	mxc_set_gpio_dataout(MX31_PIN_GPIO3_0, 1);
	mdelay(3);
}

static int udc_resume(struct arcotg_udc *udc);

static void fsl_udc_low_power_resume(struct arcotg_udc *udc)
{
	int timeout = 10;
	unsigned long flags;

	if (recovery_mode)
		return;

	if (!udc->suspended) {
		return;
	}
	/* Don't get into low power IDLE */
	atomic_set(&usb_dma_doze_ref_count, 1);

	/* Check if CPU is scaling */
        while (atomic_read(&usb_clk_change) && (timeout != 0)) {
                schedule();
                timeout--;
        }
	spin_lock_irqsave(&udc->lock, flags);
	clk_enable(usb_ahb_clk);
	spin_unlock_irqrestore(&udc->lock, flags);

	udelay(1000); /* Propagation time after clock enable */

	udc->suspended = 0;
	resume_port();

	/* SMSC PHY needs a reset */	
	if (SMSC_ID == local_phy_ops->id)
		reset_smsc_phy();

	udc_resume(udc);	
	resume_ctrl();
	enable_irq(ARCOTG_IRQ);
	resume_irq(udc);
}

static int is_usb_connected(void)
{
	t_sensor_bits sensors;

	pmic_get_sensors(&sensors);

	//Check VBUS status and charger status from Atlas
	if (sensors.sense_usb4v4s || (sensors.sense_chgcurrs && sensors.sense_chgdets))
		return 1;

	return 0;
}

static void udc_charger_timer_func(unsigned long arg)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)arg;
	u32 portsc = le32_to_cpu(usb_slave_regs->portsc1);
	unsigned long flags;

	/* Charger timer deleted and so no need to rerun it */
	if (udc_charger_timer_deleted)
		return;

	if ((portsc & PORTSCX_LINE_STATUS_BITS) == PORTSCX_LINE_STATUS_UNDEF) {
		LOG_DPDM(INFO, "cc2", "USB charger type 2 was previously misdetected as a host\n", (portsc & PORTSCX_LINE_STATUS_KSTATE) != 0, (portsc & PORTSCX_LINE_STATUS_JSTATE) != 0);
		udc->conn_sts = USB_CONN_CHARGER;
		atomic_set(&udc->timer_scheduled, 0);

		charger_handle_charging();
		charger_misdetect_retry = 0;
		atomic_set(&arcotg_busy, 0);
		return;
	}

	if (--udc->charger_retries) {
		spin_lock_irqsave(&udc->lock, flags);
		reset_irq(udc);
		spin_unlock_irqrestore(&udc->lock, flags);
		mod_timer(&udc->charger_timer, jiffies + msecs_to_jiffies(1000));
	} else {
		/*
		 * This code adds support for the following chargers:
		 *
		 * 1) iPhone charger
		 * 2) Car charger
		 * 3) Powered Hubs
		 *
		 * All the sources that can provide a VBUS work and we set the
		 * current limit to 500mA. 
		 *
		 * For non-Powered HUBs, there is no VBUS and so we are OK.
		 */
		charger_misdetect_retry = 0;
		atomic_set(&udc->timer_scheduled, 0);
		udc->conn_sts = USB_CONN_CHARGER;
		atomic_set(&arcotg_busy, 0);

		if (!udc_full_sr) {
			udc_full_sr = 1;
			/* All attempts to enumerate failed */
			udc_full_suspend_resume(udc);
		}
		else {
			printk(KERN_INFO "Detected 3rd party charger: 500mA current limit\n");
			/* Do 500mA charging */
			charger_set_current_limit(500);
		}
	}
}

static void get_usb_connection_status(struct arcotg_udc *udc)
{
	u32 portsc;
	u32 portsc1;
	u32 usbcmd;
	unsigned long flags;
	int suspended;

	spin_lock_irqsave(&udc->lock, flags);
	udc->conn_sts = USB_CONN_HOST;
	suspended = udc->suspended;
	spin_unlock_irqrestore(&udc->lock, flags);

	if (suspended)
		fsl_udc_low_power_resume(udc);

	spin_lock_irqsave(&udc->lock, flags);

	portsc = le32_to_cpu(usb_slave_regs->portsc1);
	usbcmd = le32_to_cpu(usb_slave_regs->usbcmd);

	mdelay(1);

	portsc1 = le32_to_cpu(usb_slave_regs->portsc1);
	if ((portsc1 & PORTSCX_LINE_STATUS_KSTATE) &&
		(portsc1 & PORTSCX_LINE_STATUS_JSTATE)) {
		udc->conn_sts = USB_CONN_CHARGER;
		goto exit;
	}

	portsc1 = le32_to_cpu(usb_slave_regs->portsc1);
	//If D+ and/or D- are pulled high assume we are connected to a charger
	if ((portsc1 & PORTSCX_LINE_STATUS_BITS) != PORTSCX_LINE_STATUS_SE0) {
		LOG_DPDM(INFO, "cc1", "USB connected to a type 1 charger\n", (portsc1 & PORTSCX_LINE_STATUS_KSTATE) != 0, (portsc1 & PORTSCX_LINE_STATUS_JSTATE) != 0);
	}

	mdelay(20);

	//In all other cases, we are connected to a host
	LOG_DPDM(INFO, "chs", "USB connected to a host\n", (portsc1 & PORTSCX_LINE_STATUS_KSTATE) != 0, (portsc1 & PORTSCX_LINE_STATUS_JSTATE) != 0);

	if (atomic_read(&udc->timer_scheduled)) {
		delete_charger_timer(udc);
	}
	//Set the timer to make sure we didn't misdetect
	charger_misdetect_retry = 1;
	udc->charger_retries = 3;
	atomic_set(&udc->timer_scheduled, 1);
	mod_timer(&udc->charger_timer, jiffies + msecs_to_jiffies(1000));
exit:
	spin_unlock_irqrestore(&udc->lock, flags);
}

static void chgconn_event(struct arcotg_udc *udc)
{
	int junk;

	atomic_set(&arcotg_busy, 1);

	get_usb_connection_status(udc);
	
	switch (udc->conn_sts) {
		case USB_CONN_HOST:
			UDC_LOG(INFO, "hsd", "Host detected\n");
			atomic_set(&udc->mA, 100);
			charger_set_current_limit(100);
			fsl_udc_low_power_resume(udc);
			(void)kobject_uevent(&udc->gadget.dev.parent->kobj, KOBJ_ADD);
			junk = device_create_file(udc->gadget.dev.parent,
					&dev_attr_connected);
			break;
		case USB_CONN_CHARGER:
			UDC_LOG(INFO, "chd", "Charger detected\n");
			charger_handle_charging();
			(void)kobject_uevent(&udc->gadget.dev.parent->kobj, KOBJ_ADD);
			junk = device_create_file(udc->gadget.dev.parent,
					&dev_attr_connected);
			break;
		default:
			UDC_LOG(ERROR, "unkc", "Unknown USB connection state %d\n", udc->conn_sts);
			break;
	}

	if (atomic_read(&udc->timer_scheduled) == 0)
		atomic_set(&arcotg_busy, 0);
}

static void chgdisc_event(struct arcotg_udc *udc)
{
	atomic_set(&arcotg_busy, 1);

	udc->conn_sts = USB_CONN_DISCONNECTED;
	atomic_set(&udc->mA, 0);
	charger_set_current_limit(0);
	(void)kobject_uevent(&udc->gadget.dev.parent->kobj, KOBJ_REMOVE);
	delete_charger_timer(udc);
	udc_full_sr = 0;

	if (udc->driver)
		if (udc->driver->disconnect)
			udc->driver->disconnect(&udc->gadget);

	mdelay(100);
	UDC_LOG(INFO, "udsc", "USB disconnected\n");
	device_remove_file(udc->gadget.dev.parent, &dev_attr_connected);
	fsl_udc_low_power_suspend(udc);
	cancel_rearming_delayed_work(&udc->charger_detect_work);
	atomic_set(&port_reset_counter, 0);

	/* No need to run the workqueue as this one is disconnected */
	atomic_set(&udc->run_workqueue, 0);
	/* 30 second work is no longer scheduled */
	udc->work_scheduled = 0;
	atomic_set(&arcotg_busy, 0);

	/* Charger detection timer flag cleared out */
	udc_charger_timer_deleted = 0;
}

static int chg_event(void *param, int connected)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)param;

	if (atomic_read(&udc->system_suspended))
		return -EAGAIN;

	atomic_set(&udc->run_workqueue, 0);

	if (atomic_read(&arcotg_busy))
		return -EBUSY;

	if (connected) {
		fsl_udc_low_power_resume(udc);
		if (udc->conn_sts != USB_CONN_HOST) {
			chgconn_event(udc);
		}
	} else {
		chgdisc_event(udc);
	}

	return 0;
}

static void init_detect_work(struct work_struct *work)
{
	struct arcotg_udc *udc = container_of((struct delayed_work *)work, struct arcotg_udc, work);

	udc->work_scheduled = 0;

	if (!atomic_cmpxchg(&udc->run_workqueue, 1, 0))
		return;

	if (is_usb_connected()) {
		//If we did not get configured by a host, do the detection
		if (udc->conn_sts != USB_CONN_HOST)
			chgconn_event(udc);
	} else {
		chgdisc_event(udc);
	}
}

static void udc_phy_locked_work(struct work_struct *work)
{
	UDC_LOG(CRIT, "lck", "USB Phy locked, rebooting\n");
	/* Clear out MEMA bit #1*/
	pmic_write_reg(REG_MEMORY_A, (0 << 1), (1 << 1));
	pmic_write_reg(REG_MEMORY_A, (1 << 1), (1 << 1));
	kernel_restart(NULL);
}
#endif

/* Driver probe functions */

 /*! 
  * all intialize operations implemented here except Enable usb_intr reg
  * @param dev device controller pointer
  * @return Returns zero on success , or a negative error code
  */
static int __devinit fsl_udc_probe(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;
	struct arcotg_udc *udc;

	unsigned int tmp_status = -ENODEV;
	unsigned int i;
	u32 id;
	u64 rsrc_start, rsrc_len;
	int junk;
#ifdef CONFIG_MACH_LAB126
	u16 phy_id;
#endif

	if (strcmp(pdev->name, "arc_udc")) {
		pr_debug("udc Wrong device\n");
		return -ENODEV;
	}

	pr_debug("%s pdev=0x%p  pdata=0x%p\n", __FUNCTION__, pdev, pdata);

	if (board_init(pdev) != 0)
		return -EINVAL;

	/* Initialize the udc structure including QH member and other member */
	udc = (struct arcotg_udc *)struct_udc_setup(pdev);
	udc_controller = udc;

	if (!udc) {
		pr_debug("udc udc is NULL\n");
		return -ENOMEM;
	}

	dev_set_drvdata(&pdev->dev, udc);

	udc->pdata = pdata;
	udc->xcvr_type = pdata->xcvr_type;

#ifdef CONFIG_USB_OTG
	udc->transceiver = otg_get_transceiver();
	pr_debug("udc otg_get_transceiver returns 0x%p", udc->transceiver);
#endif

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		return -ENODEV;
	}

	rsrc_start = pdev->resource[0].start;
	rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	pr_debug("start=0x%lx   end=0x%lx\n",
		 (unsigned long)pdev->resource[0].start,
		 (unsigned long)pdev->resource[0].end);
	pr_debug("rsrc_start=0x%llx  rsrc_len=0x%llx\n", rsrc_start, rsrc_len);

	usb_slave_regs = (struct usb_dr_device *)(int)IO_ADDRESS(rsrc_start);

	pr_debug("udc usb_slave_regs = 0x%p\n", usb_slave_regs);
	pr_debug("udc hci_version=0x%x\n", usb_slave_regs->hciversion);
	pr_debug("udc otgsc at 0x%p\n", &usb_slave_regs->otgsc);

#ifdef CONFIG_MACH_LAB126
	usb_ahb_clk = clk_get(NULL, "usb_ahb_clk");
#endif

	id = usb_slave_regs->id;
	UDC_LOG(INFO, "prb", "ARC USBOTG h/w ID=0x%x  revision=0x%x\n",
	       id & 0x3f, id >> 16);

	/* request irq and disable DR  */
	tmp_status = request_irq(pdev->resource[1].start, arcotg_udc_irq,
				 IRQF_SHARED, driver_name, udc);
	if (tmp_status != 0) {
		UDC_LOG(ERROR, "irq", "cannot request irq %d err %d \n",
		       (int)pdev->resource[1].start, tmp_status);
		/* DDD free mem_region here */
		return tmp_status;
	}

#ifdef CONFIG_MACH_LAB126
	INIT_DELAYED_WORK(&udc->phy_locked_work, udc_phy_locked_work);
	schedule_delayed_work(&udc->phy_locked_work, msecs_to_jiffies(6000));
	INIT_DELAYED_WORK(&udc->charger_detect_work, do_charger_detect_work);

	mdelay(3);
	phy_id = isp1504_read(0x1, &(usb_slave_regs->ulpiview)) << 8 |
		isp1504_read(0x0, &(usb_slave_regs->ulpiview));
	LOG_PHY(INFO, "phyid", "Phy manufacturer ID\n", phy_id);

	for (i = 0; i < ARRAY_SIZE(phy_list); i++) {
		if (phy_list[i].id == phy_id) {
			local_phy_ops = phy_list + i;
			break;
		}
	}

	//NXP by default
	if (NULL == local_phy_ops) {
		LOG_PHY(ERROR, "unkphy", "Unknown Phy manufacturer defaulting to NXP\n", phy_id);
		local_phy_ops = phy_list;
	}

	if (SMSC_ID == local_phy_ops->id) {
		local_phy_ops->reset();
	}
#endif
	if (!udc->transceiver) {
		/* initialize usb hw reg except for regs for EP,
		 * leave usbintr reg untouched*/
		dr_controller_setup(udc);
	}

	/* here comes the stand operations for probe
	 * set the arcotg_udc->gadget.xxx
	 */
	udc->gadget.ops = &arcotg_gadget_ops;
	udc->gadget.is_dualspeed = 1;

	/* gadget.ep0 is a pointer */
	udc->gadget.ep0 = &udc->eps[0].ep;

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	udc->gadget.speed = USB_SPEED_UNKNOWN;

	/* name: Identifies the controller hardware type. */
	udc->gadget.name = driver_name;

	device_initialize(&udc->gadget.dev);

	strcpy(udc->gadget.dev.bus_id, "gadget");

	udc->gadget.dev.release = fsl_udc_release;
	udc->gadget.dev.parent = &pdev->dev;

	if (udc->transceiver) {
		udc->gadget.is_otg = 1;
	}

#ifdef CONFIG_MACH_LAB126
	// Disable OTG Wakeup Interrupt
	usb_slave_regs->control &= cpu_to_le32(~(USBCTRL_ULPI_INT_EN | USBCTRL_OTG_WAKE_INT_EN));
	// Disable VBUS detection
	usb_slave_regs->control |= cpu_to_le32(USBCTRL_OTG_POWER_MASK);

	udc->irq = pdev->resource[1].start;
	udc->conn_sts = USB_CONN_DISCONNECTED;
	udc->work_scheduled = 0;
	atomic_set(&udc->run_workqueue, 0);
	INIT_DELAYED_WORK(&udc->work, init_detect_work);
	INIT_DELAYED_WORK(&udc->wakeup, wakeup_work);
	setup_timer(&udc->charger_timer, udc_charger_timer_func,
		    (unsigned long)udc);
	// Continue violating the USB spec to survive low battery booting
	atomic_set(&udc->mA, 300);
	charger_set_current_limit(300);
	atomic_set(&udc->system_suspended, 0);

	// Subscribe to USB cable connection events
	charger_set_arcotg_callback(chg_event, udc);
#endif

	/* for an EP, the intialization includes: fields in QH, Regs,
	 * arcotg_ep struct */
	ep0_dr_and_qh_setup(udc);
	for (i = 0; i < USB_MAX_PIPES; i++) {
		/* because the ep type is not decide here so
		 * struct_ep_qh_setup() and dr_ep_setup()
		 * should be called in ep_enable()
		 */
		if (ep_name[i] != NULL)
			/* setup the arcotg_ep struct and link ep.ep.list
			 * into gadget.ep_list */
			struct_ep_setup(udc, i);
	}

	create_proc_file();
	tmp_status = device_add(&udc->gadget.dev);

#ifdef CONFIG_CPU_FREQ
	udc->cpufreq_transition.notifier_call = arcotg_cpufreq_notifier;
	cpufreq_register_notifier(&udc->cpufreq_transition,
					CPUFREQ_TRANSITION_NOTIFIER);
#endif

#ifdef CONFIG_MACH_LAB126
	if (is_usb_connected()) {
		(void)kobject_uevent(&udc->gadget.dev.parent->kobj, KOBJ_ADD);
		junk = device_create_file(udc->gadget.dev.parent, &dev_attr_connected);
	}
	atomic_set(&udc->run_workqueue, 1);
	schedule_delayed_work(&udc->work, msecs_to_jiffies(30000));
	udc->work_scheduled = 1;

	cancel_rearming_delayed_work(&udc->phy_locked_work);
#endif
	pr_debug("udc back from device_add ");

	junk = device_create_file(udc->gadget.dev.parent, &dev_attr_log_mask);
#ifdef CONFIG_MACH_LAB126
	junk = device_create_file(udc->gadget.dev.parent, &dev_attr_manufacturer);
#endif

	return tmp_status;
}

/*! 
 * Driver removal functions
 * Free resources
 * Finish pending transaction
 * @param dev device controller pointer
 * @return Returns zero on success , or a negative error code
 */
static int __devexit fsl_udc_remove(struct platform_device *pdev)
{
	struct arcotg_udc *udc =
	    (struct arcotg_udc *)platform_get_drvdata(pdev);
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;

	DECLARE_COMPLETION(done);

	if (!udc)
		return -ENODEV;

	device_remove_file(udc->gadget.dev.parent, &dev_attr_log_mask);

#ifdef CONFIG_MACH_LAB126
	device_remove_file(udc->gadget.dev.parent, &dev_attr_manufacturer);
	atomic_set(&udc->run_workqueue, 0);
	cancel_rearming_delayed_work(&udc->work);
	udc->work_scheduled = 0;
	cancel_rearming_delayed_work(&udc->wakeup);
	cancel_rearming_delayed_work(&udc->phy_locked_work);
	cancel_rearming_delayed_work(&udc->charger_detect_work);

	delete_charger_timer(udc);	

	(void)kobject_uevent(&udc->gadget.dev.parent->kobj, KOBJ_REMOVE);
	device_remove_file(udc->gadget.dev.parent, &dev_attr_connected);

	// Unsubscribe to USB cable connection events
	charger_set_arcotg_callback(NULL, NULL);
	
	fsl_udc_low_power_resume(udc);
	clk_put(usb_ahb_clk);
#endif

	udc->done = &done;

	if (udc->transceiver) {
		put_device(udc->transceiver->dev);
		udc->transceiver = 0;
	}

	/* DR has been stopped in usb_gadget_unregister_driver() */

	/* remove proc */
	remove_proc_file();

	/* free irq */
	free_irq(pdev->resource[1].start, udc);

	/* deinitialize all ep: strcut */
	/* deinitialize ep0: reg and QH */

	/* Free allocated memory */
	pr_debug("status_req->head = 0x%p\n", udc->status_req->head);
	if (udc->status_req->head) {
		pr_debug("freeing head=0x%p\n", udc->status_req->head);
		dma_pool_free(udc->dtd_pool,
			      udc->status_req->head,
			      udc->status_req->head->td_dma);
	}

	kfree(udc->status_req->req.buf);
	kfree(udc->status_req);

	if (udc->dtd_pool)
		dma_pool_destroy(udc->dtd_pool);

	device_unregister(&udc->gadget.dev);
	/* free udc --wait for the release() finished */
	wait_for_completion(&done);

	/*
	 * do platform specific un-initialization:
	 * release iomux pins, etc.
	 */
	if (pdata->platform_uninit)
		pdata->platform_uninit(pdata);

#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_notifier(&udc->cpufreq_transition,
					CPUFREQ_TRANSITION_NOTIFIER);
#endif

	return 0;
}

static int udc_suspend(struct arcotg_udc *udc)
{
	udc->stopped = 1;
	return 0;
}

/*!
 * Modify Power management attributes
 * Here we stop the DR controller and disable the irq
 * @param dev device controller pointer
 * @param state current state
 * @return The function returns 0 on success or -1 if failed
 */
static int fsl_udc_suspend(struct device *dev, pm_message_t state)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)dev_get_drvdata(dev);
	pr_debug("udc Suspend.\n");
	return udc_suspend(udc);
}

static int udc_resume(struct arcotg_udc *udc)
{

#ifdef CONFIG_USB_OTG
	int id, temp;
	id = le32_to_cpu(usb_slave_regs->otgsc) & OTGSC_STS_USB_ID;
	temp = le32_to_cpu(usb_slave_regs->otgsc) & OTGSC_IE_USB_ID;
	if ((id == 0) || (temp == 0))
		return 0;
#endif

	/*Enable DR irq reg and set controller Run */
	if (udc->stopped) {
		dr_controller_setup(udc);
		dr_controller_run(udc);
	}
	udc->usb_state = USB_STATE_ATTACHED;
	udc->ep0_state = WAIT_FOR_SETUP;
	udc->ep0_dir = 0;
	return 0;
}

/*!
 * Invoked on USB resume. May be called in_interrupt.
 * Here we start the DR controller and enable the irq
 * @param dev device controller pointer
 * @return The function returns 0 on success or -1 if failed
 */
static int fsl_udc_resume(struct device *dev)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)dev_get_drvdata(dev);
	pr_debug("udc Resume dev=0x%p udc=0x%p\n", dev, udc);

	return udc_resume(udc);
}

/*
 * FULL udc suspend/resume. We don't want to do this
 * on every low_power suspend/resume. However, once
 * the PHY fails enumeration, this is needed to recover
 * the PHY and tell the upper layers to also suspend/resume.
 */
static void redo_detect_work(struct arcotg_udc *udc)
{
	udc->work_scheduled = 0;
	atomic_set(&udc->mA, 100);

	if (!recovery_mode) {
		udc->conn_sts = USB_CONN_DISCONNECTED;
		fsl_udc_low_power_resume(udc);
		mdelay(1);
		if (SMSC_ID == local_phy_ops->id) {
			local_phy_ops->reset();
		}
		atomic_set(&udc->system_suspended, 0);
		enable_irq(udc->irq);
	}

	if (!atomic_cmpxchg(&udc->run_workqueue, 1, 0))
		return;

	if (udc->conn_sts != USB_CONN_HOST)
		chgconn_event(udc);
}

static void arcotg_full_suspend(struct arcotg_udc *udc)
{
	if (!recovery_mode) {
		disable_irq(udc->irq);
		atomic_set(&udc->system_suspended, 1);
		atomic_set(&udc->run_workqueue, 0);
		cancel_rearming_delayed_work(&udc->work);
		udc->work_scheduled = 0;
		cancel_rearming_delayed_work(&udc->wakeup);
		udc_suspend(udc);
		fsl_udc_low_power_suspend(udc);
	}
	atomic_set(&udc->mA, 0);
}

static void arcotg_full_resume(struct arcotg_udc *udc)
{
	atomic_set(&udc->run_workqueue, 1);
	redo_detect_work(udc);
}

#ifdef CONFIG_MACH_LAB126
static int arcotg_udc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)platform_get_drvdata(pdev);

	if (!recovery_mode) {
		disable_irq(udc->irq);
		atomic_set(&udc->system_suspended, 1);

		atomic_set(&udc->run_workqueue, 0);
		cancel_rearming_delayed_work(&udc->work);
		udc->work_scheduled = 0;
		cancel_rearming_delayed_work(&udc->wakeup);
		udc_suspend(udc);
		fsl_udc_low_power_suspend(udc);
	}

	atomic_set(&udc->mA, 0);

	return 0;
}

static int arcotg_udc_resume(struct platform_device *pdev)
{
	struct arcotg_udc *udc = (struct arcotg_udc *)platform_get_drvdata(pdev);

	atomic_set(&udc->run_workqueue, 1);
	schedule_delayed_work(&udc->wakeup, msecs_to_jiffies(5000));

	return 0;
}

static void wakeup_work(struct work_struct *work)
{
	struct arcotg_udc *udc = container_of((struct delayed_work *)work, struct arcotg_udc, wakeup);

	atomic_set(&udc->mA, 100);

	if (!recovery_mode) {
		udc->conn_sts = USB_CONN_DISCONNECTED;

		fsl_udc_low_power_resume(udc);
		udc_resume(udc);
		mdelay(1);
		if (SMSC_ID == local_phy_ops->id) {
			local_phy_ops->reset();
		}
		atomic_set(&udc->system_suspended, 0);
		enable_irq(udc->irq);
		init_detect_work((struct work_struct *)&udc->work);
	}
}
#endif

/*!
 * Register entry point for the peripheral controller driver
 */
static struct platform_driver udc_driver = {
	.probe = fsl_udc_probe,
	.remove = __exit_p(fsl_udc_remove),
	.driver = {
		   .name = driver_name,
		   },
#ifdef CONFIG_MACH_LAB126
	.suspend = arcotg_udc_suspend,
	.resume = arcotg_udc_resume,
#endif
};

static int __init udc_init(void)
{
	int rc;

	LLOG_INIT();

	LLOGS_DEBUG0("arcotg_udc", "%s recovery_mode = %d\n", __func__, recovery_mode);

	UDC_LOG(INFO, "drv", "%s version %s init \n", driver_desc, DRIVER_VERSION);
	rc = platform_driver_register(&udc_driver);
	pr_debug("udc %s() driver_register returns %d\n", __FUNCTION__, rc);
	return rc;
}

module_init(udc_init);

static void __exit udc_exit(void)
{
	platform_driver_unregister(&udc_driver);
}

module_exit(udc_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
