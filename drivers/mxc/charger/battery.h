/*
 * battery.h - Interface to the "battery" module
 *
 * Copyright (C) 2009 Amazon Technologies, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#define BATTERY_TEMPL	0x06
#define BATTERY_TEMPH	0x07
#define BATTERY_VOLTL	0x08
#define BATTERY_VOLTH	0x09
#define BATTERY_FLAGS	0x0A
#define BATTERY_CACL	0x10
#define BATTERY_CACH	0x11
#define BATTERY_AIL	0x14
#define BATTERY_AIH	0x15
#define BATTERY_CSOC	0x2C
#define BATTERY_DCOMP	0x7E
#define BATTERY_TCOMP	0x7F

/* Redefined registers aliases */
#define BATTERY_ID		BATTERY_DCOMP
#define BATTERY_BUILD_DATE	BATTERY_TCOMP

#define BATTERY_FLAGS_CHGS_MSK	0x80	/* FLAGS[CHGS] mask */

/* Bitmasks for battery problems */
#define BATTERY_INVALID_ID		0x0001
#define BATTERY_COMMS_FAILURE		0x0002
#define BATTERY_TEMP_OUT_OF_RANGE	0x0004
#define BATTERY_VOLTAGE_OUT_OF_RANGE	0x0008

extern int battery_error_flags;

int battery_read_reg(unsigned char reg_num, unsigned char *value);
void battery_register_callback(int (*callback)(void *, int), void *arg);

