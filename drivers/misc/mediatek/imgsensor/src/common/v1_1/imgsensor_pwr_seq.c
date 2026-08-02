/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "kd_imgsensor.h"


#include "imgsensor_hw.h"
#include "imgsensor_cfg_table.h"

/* Legacy design */
struct IMGSENSOR_HW_POWER_SEQ sensor_power_sequence[] = {
/* Onyx */
#if defined(OV13B10_MIPI_RAW)
	{
		SENSOR_DRVNAME_OV13B10_MIPI_RAW,
		{
			{SensorMCLK, Vol_High, 1},
			{RST, Vol_Low, 1},
			{AVDD, Vol_High, 1},
			{DOVDD, Vol_1800, 1},
			{DVDD, Vol_High, 5},
			{RST, Vol_High, 1},
		},
	},
#endif
#if defined(HI1339_MIPI_RAW)
	{
		SENSOR_DRVNAME_HI1339_MIPI_RAW,
		{
			{RST, Vol_Low, 1},
			{DOVDD, Vol_1800, 1},
			{AVDD, Vol_High, 1},
			{DVDD, Vol_High, 5},
			{SensorMCLK, Vol_High, 1},
			{RST, Vol_High, 1},
		},
	},
#endif
#if defined(S5K4H7_MIPI_RAW)
	{
		SENSOR_DRVNAME_S5K4H7_MIPI_RAW,
		{
			{SensorMCLK, Vol_High, 5},
			{RST, Vol_Low, 1},
			{DVDD, Vol_High, 1},
			{AVDD, Vol_High, 1},
			{DOVDD, Vol_1800, 1},
			{RST, Vol_High, 1}
		},
	},
#endif
#if defined(GC08A3_MIPI_RAW)
	{
		SENSOR_DRVNAME_GC08A3_MIPI_RAW,
		{
			{SensorMCLK, Vol_High, 5},
			{RST, Vol_Low, 1},
			{DOVDD, Vol_1800, 1},
			{DVDD, Vol_High, 1},
			{AVDD, Vol_High, 1},
			{RST, Vol_High, 1}
		},
	},
#endif

	/* add new sensor before this line */
	{NULL,},
};

