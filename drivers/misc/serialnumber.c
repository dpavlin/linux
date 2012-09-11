/*
 * serialnumber.c
 *
 * Copyright (C) 2006 Lab126, Inc.  All rights reserved. 
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>


static int
proc_usid_read(
	char *page,
	char **start,
	off_t off,
	int count,
	int *eof,
	void *data)
{
	char serial_num[BOARD_SERIALNUM_SIZE + 1];

	memset(serial_num, '\0', sizeof(serial_num));
	strncpy(serial_num, system_serial_data, BOARD_SERIALNUM_SIZE);

	strcpy(page, serial_num);

	*eof = 1;

	return strlen(page);
}


static int __init
serialnumber_init(
	void)
{
	return 0;
}


static void __exit
serialnumber_exit(
	void)
{
	// should remove proc entry, but not really necessary in this case
}


module_init(serialnumber_init);
module_exit(serialnumber_exit);

MODULE_DESCRIPTION("serialnumber driver");
MODULE_AUTHOR("Lab126");
MODULE_LICENSE("Proprietary");


