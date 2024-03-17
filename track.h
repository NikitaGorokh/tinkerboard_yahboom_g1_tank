// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024 Nikita Gorokhovatskiy
 *
 * Author: Nikita Gorohovatskiy <nikita.gorokhovatskiy@gmail.com>
 */

#ifndef TRACK_H
#define TRACK_H

#include "device.h"
#include <gpiod.h>

#define TRACK_PERIOD		20000
#define TRACK_MINTIME		2000
#define TRACK_DELTA			1000

int track_get_speed_right (struct device *dev);
int track_get_speed_left (struct device *dev);

void track_set_speed(struct device *dev, int workload_right, int workload_left);

int track_init (struct device *dev,
				struct gpiod_line *pwmb, struct gpiod_line *bin1, struct gpiod_line *bin2,
				struct gpiod_line *pwma, struct gpiod_line *ain1, struct gpiod_line *ain2);

#endif
