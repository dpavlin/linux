/*
 *	Linux kernel driver for the Stantum Multitouch Kit 15.4
 *
 *	The copyright holders for the contents of this file are:
 *		Ecole Nationale de l'Aviation Civile, France (2008-2009)
 *
 *	You can use this file, redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of the
 *	License, or (at your option) any later version.
 *
 *	Contributors:
 *		Sebastien Hamdani <sebastien.hamdani@eleves.enac.fr>
 *		Stephane Chatty <chatty@enac.fr>
 *
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>

#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Sebastien Hamdani <hamdani@lii-enac.fr>, Stephane Chatty <chatty@enac.fr>"
#define DRIVER_DESC "Stantum MultiTouch Kit 15.4"
#define VENDOR_ID 0x1f87
#define PRODUCT_ID 0x0002

struct point {
	__u16 x;
	__u16 y;
	__u16 id;
	__u8 status;
	__u8 padding;
};

struct header {
	__u8 ctrl1;
	__u8 nbpoints;
	__u8 len;
	__u8 ctrl2;
};

struct smk {
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev *tdev;
	struct input_dev *mdev;
	struct usb_interface *interface;
	__u8 bulk_in;
	__u8 bulk_out;
	struct urb *rq_urb;
	struct header header;
	unsigned char buf[1024];
	unsigned char* read_head;
};

static void smk_send_rq(struct smk *smk);

char *data;
int mouse_emulation = 1;
module_param(mouse_emulation, int, 0644);


/* the function called once we have a well formed set of contact points */
static void report_pos(struct smk *smk)
{
	struct point *p = (struct point *) &smk->buf;
	struct point *oldest_p = p + smk->header.nbpoints-1;

	data = smk->buf;
	data[1023] = smk->header.nbpoints;

	/* emit all raw positions (multitouch output)*/
	while (p <= oldest_p) {
		input_report_abs(smk->mdev, ABS_MT_TRACKING_ID, p->id);
		input_report_abs(smk->mdev, ABS_MT_POSITION_X, p->x);
		input_report_abs(smk->mdev, ABS_MT_POSITION_Y, p->y);
		input_mt_sync(smk->mdev);
		p++;
	}
	input_sync(smk->mdev);

	if (smk->header.nbpoints > 1 )
		return;

	/* emit oldest contact point as single touch */
	if (oldest_p->status) {

		if(mouse_emulation == 1)
			input_report_key(smk->tdev, BTN_LEFT, 1);
		else if(mouse_emulation > 1)
			input_report_key(smk->tdev, BTN_TOUCH, 1);
		else
			return;

		input_report_abs(smk->tdev, ABS_X, oldest_p->x);
		input_report_abs(smk->tdev, ABS_Y, oldest_p->y);
		input_sync(smk->tdev);

	} else {

		if(mouse_emulation == 1)
			input_report_key(smk->tdev, BTN_LEFT, 0);
		else if(mouse_emulation >1)
			input_report_key(smk->tdev, BTN_TOUCH, 0);

		input_sync(smk->tdev);
	}
}


/* send an init to the hardware */
static int smk_send_init(struct smk *smk)
{
	struct usb_device *udev = smk->usbdev;
	unsigned char vers_msg[4] = {1,0,4,0};
	unsigned char init_msg[4] = {2,0,4,0};
	unsigned int len;

	usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 9, 0, 1, 0, NULL, 0, HZ*10);
	usb_bulk_msg(udev, usb_sndbulkpipe(udev, smk->bulk_out),
			vers_msg, 4, &len, HZ*10);	
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, smk->bulk_in),
			 &smk->buf, 4, &len, HZ*10);
	if(smk->buf[1] != 2)
		err("Warning ! Firmware version is v%d, this driver is made for v2", smk->buf[1]);

	usb_bulk_msg(udev, usb_sndbulkpipe(udev, smk->bulk_out),
			init_msg, 4, &len, HZ*10);
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, smk->bulk_in),
			 &smk->buf, 18, &len, HZ*10);
	if (len != 18 || smk->buf[0] != 2) {
		err("init failed");
		return 0;
	}
	return 1;
}


/* Part of the payload is available, read it and decide what to do  */
static void smk_payload_ready(struct urb *urb)
{
	struct smk *smk = (struct smk*) urb->context;
	struct usb_device *udev = smk->usbdev;
	struct header* h = &smk->header;

	smk->read_head += urb->actual_length;
	h->len -= urb->actual_length;
	if (h->len > 0) {
		usb_fill_bulk_urb(smk->rq_urb, udev,
			  usb_rcvbulkpipe(udev, smk->bulk_in),
			  smk->read_head, h->len,
			  smk_payload_ready, smk);
		if (usb_submit_urb(smk->rq_urb, GFP_ATOMIC) < 0)
			err("re-asking payload failed");
		return;
	}
	report_pos(smk);

	/* start over */
	smk_send_rq(smk);
}


/* A header is available, read it then decide what to do  */
static void smk_header_ready(struct urb *urb)
{
	struct smk *smk = (struct smk*) urb->context;
	struct usb_device *udev = smk->usbdev;
	struct header* h = &smk->header;

	if (urb->status) {
		err("asking header failed with status %d", urb->status);
		return;
	}
        if (h->ctrl1 != 3 || h->ctrl2 != 0 || h->len != h->nbpoints*8 + 4) {
		goto next;
	}

	h->len -= 4;
	if (h->nbpoints > 0) {
		smk->read_head = smk->buf;
		usb_fill_bulk_urb(smk->rq_urb, udev,
			  usb_rcvbulkpipe(udev, smk->bulk_in),
			  &smk->buf, h->len,
			  smk_payload_ready, smk);
		if (usb_submit_urb(smk->rq_urb, GFP_ATOMIC) < 0)
			err("asking payload failed");
		return;
	} else
		report_pos(smk);
next:
	smk_send_rq (smk);
}


/*  A request was properly sent, time to read the header of the response */
static void smk_rq_sent(struct urb *urb)
{
	struct smk *smk = (struct smk*) urb->context;
	struct usb_device *udev = smk->usbdev;

	usb_fill_bulk_urb(smk->rq_urb, udev,
			  usb_rcvbulkpipe(udev, smk->bulk_in),
			  &smk->header, 4,
			  smk_header_ready, smk);
	if (usb_submit_urb(smk->rq_urb, GFP_ATOMIC) < 0)
		err("asking response failed");
}

/*  send a request to the hardware */
static void smk_send_rq(struct smk *smk)
{
	struct usb_device *udev = smk->usbdev;
	unsigned char msg[4] = {3,0,4,0};

	usb_fill_bulk_urb(smk->rq_urb, udev,
			  usb_sndbulkpipe(udev, smk->bulk_out),
			  msg, 4,
			  smk_rq_sent, smk);
	if (usb_submit_urb(smk->rq_urb, GFP_ATOMIC) < 0)
		err("sending rq failed");
}

static int smk_open(struct input_dev *dev)
{
	struct smk *smk = input_get_drvdata(dev);

	smk_send_rq(smk);
	return 0;
}

static void smk_close(struct input_dev *dev)
{
	struct smk *smk = input_get_drvdata(dev);

	usb_kill_urb(smk->rq_urb);
}

static void smk_free(struct smk *smk)
{
	usb_put_dev(smk->usbdev);
	kfree(smk);
}

static int open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t smk_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	if(data)
		if (copy_to_user(buf, data, count))
			return -EFAULT;
	return count;
}

static const struct file_operations smk_fops = {
	.open = open,
	.read =	smk_read,
};

static struct usb_class_driver smk_class = {
	.name =		"smk%d",
	.fops =		&smk_fops,
};


static int smk_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int retval = -ENOMEM;
	int i;
	struct usb_host_interface *idesc = interface->cur_altsetting;
	struct usb_device *udev = usb_get_dev(interface_to_usbdev(interface));
	struct usb_device_descriptor *udesc = &udev->descriptor;

	/* 1. allocate an internal structure, and two input devices */
	struct smk *smk = kzalloc(sizeof(struct smk), GFP_KERNEL);
	struct input_dev *tdev = input_allocate_device();
	struct input_dev *mdev = input_allocate_device();
	if (!smk || !tdev || !mdev)
		goto error;

	/* 2. establish cross-references */
	smk->interface = interface;
	smk->usbdev = udev;
	smk->tdev = tdev;
	smk->mdev = mdev;
	usb_set_intfdata(interface, smk);
	input_set_drvdata(mdev, smk);
	input_set_drvdata(tdev, smk);

	/* 3. set up the endpoint information */
	for (i = 0; i < idesc->desc.bNumEndpoints; ++i) {
		struct usb_endpoint_descriptor *ep = &idesc->endpoint[i].desc;

		if (!smk->bulk_in &&
		    usb_endpoint_is_bulk_in(ep))
			smk->bulk_in = ep->bEndpointAddress;

		if (!smk->bulk_out &&
		    usb_endpoint_is_bulk_out(ep))
			smk->bulk_out = ep->bEndpointAddress;
	}
	if (!(smk->bulk_in && smk->bulk_out)) {
		err("Could not find both bulk-in and bulk-out endpoints");
		goto error;
	}


	/* 4. prepare the URB periodically used for requesting data */
	smk->rq_urb = usb_alloc_urb (0, GFP_ATOMIC);
	if (!smk->rq_urb) {
		retval = -ENOMEM;
		goto error;
	}

	/* 5. set physical path of the device */	
	usb_make_path(udev, smk->phys, sizeof(smk->phys));
	strlcat(smk->phys, "/input0", sizeof(smk->phys));

	/* 6. Register the device */
	retval = usb_register_dev(interface, &smk_class);
	if (retval) {
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* 7. initialize the hardware device */
	if (! smk_send_init (smk)) {
		retval = -EFAULT;
		goto error;
	}

	/* 8. initialise the two input devices */

	/* 8.1 use the same id data */
	mdev->name = tdev->name = "Stantum Multitouch Kit";
	mdev->phys = tdev->phys = smk->phys;
	mdev->id.bustype = tdev->id.bustype = BUS_USB;
	mdev->id.vendor = tdev->id.vendor = le16_to_cpu(udesc->idVendor);
	mdev->id.product = tdev->id.product = le16_to_cpu(udesc->idProduct);
	mdev->id.version = tdev->id.version = le16_to_cpu(udesc->bcdDevice);
	mdev->dev.parent = tdev->dev.parent = &interface->dev;

	/* 8.2 a touchscreen emulation */
	tdev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	if(mouse_emulation == 1)
			tdev->keybit[BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT);
		else if(mouse_emulation >1)
			tdev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(tdev, ABS_X, 0, 65535, 0, 0);
	input_set_abs_params(tdev, ABS_Y, 0, 65535, 0, 0);

	/* 8.3 the mutitouch device, designed not to be seen as a mouse */
	mdev->evbit[0] = BIT_MASK(EV_KEY)
				| BIT_MASK(EV_ABS) | BIT_MASK(EV_MSC);
	mdev->mscbit[BIT_WORD(EV_MSC)] = BIT_MASK(MSC_SERIAL);
	mdev->keybit[BIT_WORD(BTN_TOOL_FINGER)] = BIT_MASK(BTN_TOOL_FINGER);
	input_set_abs_params(mdev, ABS_MT_POSITION_X, 0, 65535, 0, 0);
	input_set_abs_params(mdev, ABS_MT_POSITION_Y, 0, 65535, 0, 0);
	input_set_abs_params(mdev, ABS_MT_TRACKING_ID, 0, 65535, 0, 0);


	/* 8.4 use the same open and close functions */
	tdev->open = smk_open;
	tdev->close = smk_close;


	/* 9. register the input devices with the input subsystem */
	if (input_register_device(tdev) != 0)
		goto error;
	if (input_register_device(mdev) != 0)
		goto error;

	return 0;

error:
	if (smk)
		smk_free(smk);
	return retval;
}

static void smk_disconnect(struct usb_interface *intf)
{
	struct smk *smk = usb_get_intfdata(intf);

	usb_set_intfdata(intf, 0);
	usb_deregister_dev(intf, &smk_class);

	if (smk) {
		usb_kill_urb(smk->rq_urb);
		input_unregister_device(smk->mdev);
		input_unregister_device(smk->tdev);
		smk_free(smk);
	}
}

static struct usb_device_id smk_id_table [] = {
	{ USB_DEVICE( VENDOR_ID, PRODUCT_ID) },
	{ }
};

static struct usb_driver smk_driver = {
	.name		= "Stantum Multitouch Kit",
	.id_table	= smk_id_table,
	.probe		= smk_probe,
	.disconnect	= smk_disconnect,
	.no_dynamic_id	= 1,
};


MODULE_DEVICE_TABLE (usb, smk_id_table);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_PARM_DESC(mouse_emulation, "<=0 to disable, 1 to send BTN_LEFT events, >1 to send BTN_TOUCH events (not always supported by the X-server)");

static int __init smk_module_init (void)
{
	int retval = usb_register(&smk_driver);
	/*if (retval == 0)
		info(DRIVER_VERSION ":" DRIVER_DESC);*/
	return retval;
}

static void __exit smk_module_exit (void)
{
	usb_deregister(&smk_driver);
}

module_init(smk_module_init);
module_exit(smk_module_exit);
