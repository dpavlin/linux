/*
 * mwan.h  --  Mario WAN hardware control driver
 *
 * Copyright 2005-2008 Lab126, Inc.  All rights reserved.
 *
 */

#ifndef __MWAN_H__
#define __MWAN_H__

typedef enum {
	WAN_INVALID = -1,
	WAN_OFF,
	WAN_ON,
	WAN_OFF_KILL
} wan_status_t;


extern void wan_set_power_status(wan_status_t);
extern wan_status_t wan_get_power_status(void);

#endif // __MWAN_H__

