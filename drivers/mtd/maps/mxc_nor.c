/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * (c) 2005 MontaVista Software, Inc.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/clocksource.h>
#include <linux/proc_fs.h>
#include <asm/mach-types.h>
#include <asm/mach/flash.h>

#define DVR_VER "2.0"

#ifdef CONFIG_MACH_MX31ADS
#define BOARD_ID_OFFSET ((2 * 512 * 1024) + (2 * 1024 * 1024))  // From mach-mx3/mx31ads.c, MTD 2, "userfs".
#else
#define BOARD_ID_OFFSET 0x8000                                  // From mach-mx3/mario.c, MTD 2, "BoardId".
#endif

#define SERIAL_NUM_BASE          0
#define SERIAL_NUM_SIZE         16

#define BOARD_ID_BASE           (SERIAL_NUM_BASE + SERIAL_NUM_SIZE)
#define BOARD_ID_SIZE           16

#define PANEL_ID_BASE           (BOARD_ID_BASE + BOARD_ID_SIZE)
#define PANEL_ID_SIZE           32

#define PCB_ID_BASE		516
#define PCB_ID_SIZE		8


#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "RedBoot", "cmdlinepart", NULL };
#endif

struct clocksource *mtd_xip_clksrc;

struct mxcflash_info {
	struct mtd_partition *parts;
	struct mtd_info *mtd;
	struct map_info map;
};

char *board_id_addr;

char mx31_serial_number[SERIAL_NUM_SIZE + 1];
EXPORT_SYMBOL(mx31_serial_number);

char mx31_board_id[BOARD_ID_SIZE + 1];
EXPORT_SYMBOL(mx31_board_id);

char mx31_panel_id[PANEL_ID_SIZE + 1];
EXPORT_SYMBOL(mx31_panel_id);

char mx31_pcb_id[PCB_ID_SIZE + 1];
EXPORT_SYMBOL(mx31_pcb_id);

static int proc_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data, char *id)
{
	strcpy(page, id);
	*eof = 1;
	
	return strlen(page);
}

#define PROC_ID_READ(id) proc_id_read(page, start, off, count, eof, data, id)

static int proc_usid_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(mx31_serial_number);
}

static int proc_board_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(mx31_board_id);
}

static int proc_panel_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
        return PROC_ID_READ(mx31_panel_id);
}

static int proc_pcb_id_read(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
	return PROC_ID_READ(mx31_pcb_id);
}

static void mx31_serialnumber_init(void)
{
	struct proc_dir_entry *proc_usid = create_proc_entry("usid", S_IRUGO, NULL);
	struct proc_dir_entry *proc_board_id = create_proc_entry("board_id", S_IRUGO, NULL);
	struct proc_dir_entry *proc_panel_id = create_proc_entry("panel_id", S_IRUGO, NULL);
	struct proc_dir_entry *proc_pcb_id = create_proc_entry("pcbsn", S_IRUGO, NULL);

	if (proc_usid != NULL) {
		proc_usid->data = NULL;
		proc_usid->read_proc = proc_usid_read;
		proc_usid->write_proc = NULL;
	}

	if (proc_board_id != NULL) {
		proc_board_id->data = NULL;
		proc_board_id->read_proc = proc_board_id_read;
		proc_board_id->write_proc = NULL;
	}

	if (proc_panel_id != NULL) {
		proc_panel_id->data = NULL;
		proc_panel_id->read_proc = proc_panel_id_read;
		proc_panel_id->write_proc = NULL;
	}

	if (proc_pcb_id != NULL) {
		proc_pcb_id->data = NULL;
		proc_pcb_id->read_proc = proc_pcb_id_read;
		proc_pcb_id->write_proc = NULL;
	}
}

static void mx31_serial_board_numbers(void)
{
	unsigned char ch;
	unsigned long i;
	int j;

	for (i=SERIAL_NUM_BASE, j=0; j<SERIAL_NUM_SIZE; i++) {
		ch =  __raw_readb(board_id_addr + i);
		mx31_serial_number[j++] = ch;
	}
	mx31_serial_number[j] = '\0';

	for (i=BOARD_ID_BASE, j=0; j<BOARD_ID_SIZE; i++) {
		ch =  __raw_readb(board_id_addr + i);
		mx31_board_id[j++] = ch;
	}
	mx31_board_id[j] = '\0';

	for (i=PANEL_ID_BASE, j=0; j<PANEL_ID_SIZE; i++) {
		ch =  __raw_readb(board_id_addr + i);
		mx31_panel_id[j++] = ch;
	}
	mx31_panel_id[j] = '\0';

	for (i=PCB_ID_BASE, j=0; j<PCB_ID_SIZE; i++) {
		ch =  __raw_readb(board_id_addr + i);
		mx31_pcb_id[j++] = ch;
	}
	mx31_pcb_id[j] = '\0';

	// Removed per official request to rid the log of FSN 
	//printk ("MX31 Serial Number - %s\n", mx31_serial_number);
	printk ("MX31 Board id - %s\n", mx31_board_id);
	printk ("MX31 Panel id - %s\n", mx31_panel_id);
	printk ("MX31 PCB id - %s\n", mx31_pcb_id);
	
	memset(system_serial_data, '\0', BOARD_SERIALNUM_SIZE);
	strncpy(system_serial_data, mx31_serial_number, SERIAL_NUM_SIZE);
}

/*!
 * @defgroup NOR_MTD NOR Flash MTD Driver
 */

/*!
 * @file mxc_nor.c
 *
 * @brief This file contains the MTD Mapping information on the MXC.
 *
 * @ingroup NOR_MTD
 */

static int __devinit mxcflash_probe(struct platform_device *pdev)
{
	int err, nr_parts = 0;
	struct mxcflash_info *info;
	struct flash_platform_data *flash = pdev->dev.platform_data;
	struct resource *res = pdev->resource;
	unsigned long size = res->end - res->start + 1;

	info = kzalloc(sizeof(struct mxcflash_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (!request_mem_region(res->start, size, "flash")) {
		err = -EBUSY;
		goto out_free_info;
	}

	info->map.virt = ioremap(res->start, size);
	if (!info->map.virt) {
		err = -ENOMEM;
		goto out_release_mem_region;
	}
	
	info->map.name = pdev->dev.bus_id;
	info->map.phys = res->start;
	info->map.size = size;
	info->map.bankwidth = flash->width;

	mtd_xip_clksrc = clocksource_get_next();

	simple_map_init(&info->map);
	info->mtd = do_map_probe(flash->map_name, &info->map);
	if (!info->mtd) {
		err = -EIO;
		goto out_iounmap;
	}
	info->mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	nr_parts =
	    parse_mtd_partitions(info->mtd, part_probes, &info->parts, 0);
	if (nr_parts > 0) {
		add_mtd_partitions(info->mtd, info->parts, nr_parts);
	} else if (nr_parts < 0 && flash->parts) {
		add_mtd_partitions(info->mtd, flash->parts, flash->nr_parts);
	} else
#endif
	{
		printk(KERN_NOTICE "MXC flash: no partition info "
		       "available, registering whole flash\n");
		add_mtd_device(info->mtd);
	}

	platform_set_drvdata(pdev, info);

	board_id_addr = info->map.virt + BOARD_ID_OFFSET; /* MTD 2 */
	mx31_serial_board_numbers();
	mx31_serialnumber_init();

	return 0;

      out_iounmap:
	iounmap(info->map.virt);
      out_release_mem_region:
	release_mem_region(res->start, size);
      out_free_info:
	kfree(info);

	return err;
}

static int __devexit mxcflash_remove(struct platform_device *pdev)
{

	struct mxcflash_info *info = platform_get_drvdata(pdev);
	struct flash_platform_data *flash = pdev->dev.platform_data;

	platform_set_drvdata(pdev, NULL);

	if (info) {
		if (info->parts) {
			del_mtd_partitions(info->mtd);
			kfree(info->parts);
		} else if (flash->parts)
			del_mtd_partitions(info->mtd);
		else
			del_mtd_device(info->mtd);

		map_destroy(info->mtd);
		release_mem_region(info->map.phys, info->map.size);
		iounmap((void __iomem *)info->map.virt);
		kfree(info);
	}
	return 0;
}

#ifdef CONFIG_PM

static int mxcflash_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	struct mxcflash_info *info = platform_get_drvdata(pdev);
	int ret = 0;

	if (info->mtd && info->mtd->suspend)
		ret = info->mtd->suspend(info->mtd);

	return ret;
}

static int mxcflash_resume(struct platform_device *pdev)
{
	struct mxcflash_info *info = platform_get_drvdata(pdev);
	
	if (info->mtd && info->mtd->resume)
		info->mtd->resume(info->mtd);

	return 0;
}

static void mxcflash_shutdown(struct platform_device *pdev)
{
	struct mxcflash_info *info = platform_get_drvdata(pdev);

	if (info && info->mtd->suspend(info->mtd) == 0)
		info->mtd->resume(info->mtd);
}

#else 

#define mxcflash_suspend	NULL
#define mxcflash_resume	NULL
#define mxcflash_shutdown	NULL

#endif /* CONFIG_PM */
	

static struct platform_driver mxcflash_driver = {
	.driver = {
		   .name = "mxc_nor_flash",
		   },
	.probe = mxcflash_probe,
	.remove = __devexit_p(mxcflash_remove),
	.suspend = mxcflash_suspend,
	.resume = mxcflash_resume,
	.shutdown = mxcflash_shutdown
};

/*!
 * This is the module's entry function. It passes board specific
 * config details into the MTD physmap driver which then does the
 * real work for us. After this function runs, our job is done.
 *
 * @return  0 if successful; non-zero otherwise
 */
static int __init mxc_mtd_init(void)
{
	pr_info("MXC MTD nor Driver %s\n", DVR_VER);
	if (platform_driver_register(&mxcflash_driver) != 0) {
		printk(KERN_ERR "Driver register failed for mxcflash_driver\n");
		return -ENODEV;
	}
	return 0;
}

/*!
 * This function is the module's exit function. It's empty because the
 * MTD physmap driver is doing the real work and our job was done after
 * mxc_mtd_init() runs.
 */
static void __exit mxc_mtd_exit(void)
{
	platform_driver_unregister(&mxcflash_driver);
}

module_init(mxc_mtd_init);
module_exit(mxc_mtd_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MTD map and partitions for Freescale MXC boards");
MODULE_LICENSE("GPL");
