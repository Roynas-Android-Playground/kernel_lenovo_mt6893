/*
 *  driver interface to finger print sensor  for 
 *	Copyright (c) 2015  ChipSailing Technology.
 *	All rights reserved.
***********************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/spi/spi.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/string.h>

int BoardId = 0;
int __init setup_boardid(char *str)
{
	if (!strncmp(str, "EVB1", 4)) {
		BoardId = 1;
		return 1;
	} else if (!strncmp(str, "EVB2", 4)) {
		BoardId = 2;
		return 1;
	} else if (!strncmp(str, "EVT", 3)) {
		BoardId = 3;
		return 1;
	} else if (!strncmp(str, "DVT1", 4)) {
		BoardId = 4;
		return 1;
	} else if (!strncmp(str, "DVT2", 4)) {
		BoardId = 5;
		return 1;
	} else if (!strncmp(str, "PVT", 3)) {
		BoardId = 6;
		return 1;
	} else {
		BoardId = 0;
		return 1;
	}
}
__setup("androidboot.hwboardid=", setup_boardid);

int box_id = 0;
int audio_boxid_get(void)
{
	return box_id;
}
EXPORT_SYMBOL(audio_boxid_get);

int __init setup_boxid(char *str)
{
	box_id = simple_strtoul(str, NULL, 16);
	pr_info("box_id: %d\n", box_id);
	return 1;
}
__setup("androidboot.boxid=", setup_boxid);
