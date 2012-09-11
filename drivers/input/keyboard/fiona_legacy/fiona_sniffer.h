/******************************************************************
 *
 *  File:   fiona_sniffer.h
 *
 *  Author: Nick Vaccaro <nvaccaro@lab126.com>
 *
 *  Date:   03/29/07
 *
 *  Copyright 2007, Lab126, Inc.  All rights reserved.
 *
 *  Description:
 *      Prototypes for the queue sniffer
 *
 ******************************************************************/
#ifndef __FIONA_SNIFFER__
#define __FIONA_SNIFFER__

#include <linux/autoconf.h>

extern int  find_event_in_queue(int event);

extern int  strip_queue_for_power_on(int *onInQueue, int *wakeInQ, int *totalStripped);
extern int  strip_queue_for_power_lockout(void);
extern int  remove_paired_wan_events_in_queue(void);
extern int  remove_alt_font_keys_from_event_queue(void);
extern int  idle_wake_event_in_event_queue(void);
extern int  find_remove_paired_event_in_queue(int mainEvent, int pairedEvent);

extern int  sniff_remove_events_from_queue(int event);
extern int  sniffFixUnstallQueue(int state);
extern int  sniffFixQueue(int state);

extern int  postSniffFixUnstallQueue(int state);
extern int  post_sniff_remove_events_from_queue(FPOW_SNIFF_STATES state);

extern int  fixUnstallEventQueue(int event);
extern int  fixEventQueue(int event);

extern asmlinkage int hlog(const char *fmt, ...);
extern asmlinkage int hextlog(const char *fmt, ...);

#endif // __FIONA_SNIFFER
