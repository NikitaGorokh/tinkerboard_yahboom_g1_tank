#ifndef SONIC_H
#define SONIC_H

#include <gpiod.h>
#include "device.h"

#define SONIC_PERIOD	60000

int sonic_init (struct device *dev, struct gpiod_line *in, struct gpiod_line *out);

int sonic_get_distance (struct device *dev);

void sonic_change_mode (struct device *dev, int mode);
#endif
