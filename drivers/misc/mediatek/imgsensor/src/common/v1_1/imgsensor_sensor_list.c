/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include "kd_imgsensor.h"
#include "imgsensor_sensor_list.h"

/* Add Sensor Init function here
 * Note:
 * 1. Add by the resolution from ""large to small"", due to large sensor
 *    will be possible to be main sensor.
 *    This can avoid I2C error during searching sensor.
 * 2. This file should be the same as
 *    mediatek\custom\common\hal\imgsensor\src\sensorlist.cpp
 */
struct IMGSENSOR_SENSOR_LIST
	gimgsensor_sensor_list[MAX_NUM_OF_SUPPORT_SENSOR] = {
/* Onyx */
#if defined(OV13B10_MIPI_RAW)
	{OV13B10_SENSOR_ID, SENSOR_DRVNAME_OV13B10_MIPI_RAW, OV13B10_MIPI_RAW_SensorInit},
#endif
#if defined(HI1339_MIPI_RAW)
	{HI1339_SENSOR_ID, SENSOR_DRVNAME_HI1339_MIPI_RAW, HI1339_MIPI_RAW_SensorInit},
#endif
#if defined(S5K4H7_MIPI_RAW)
	{S5K4H7_SENSOR_ID, SENSOR_DRVNAME_S5K4H7_MIPI_RAW, S5K4H7_MIPI_RAW_SensorInit},
#endif
#if defined(GC08A3_MIPI_RAW)
	{GC08A3_SENSOR_ID, SENSOR_DRVNAME_GC08A3_MIPI_RAW, GC08A3_MIPI_RAW_SensorInit},
#endif

	/*  ADD sensor driver before this line */
	{0, {0}, NULL}, /* end of list */
};

/* e_add new sensor driver here */

