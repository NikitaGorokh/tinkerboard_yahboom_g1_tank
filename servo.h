// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024 Andrey Kshevetskiy
 *
 * Author: Andrey Kshevetskiy <andrey.kshevetskiy@gmail.com>
 */
 
#ifndef SERVO_H
#define SERVO_H

#include <gpiod.h>
#include "device.h"

void angle_set (struct device *dev, int angle);

int angle_get (struct device *dev);

int angle_min (struct device *dev);
int angle_max (struct device *dev);
int angle_def (struct device *dev);

int angle_servo_init (struct device *dev,
						int min_angle, int max_angle, int def_angle,
						struct gpiod_line *out);

#endif
