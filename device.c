// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2023-2024 Mikhail Kshevetskiy
 *
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@gmail.com>
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"

int device_stop_request(struct device *dev)
{
	switch(dev->state) {
	    case DEV_STATE_STOPPED:
	    case DEV_STATE_STOPPING:
		break;
	    default:
		dev->state = DEV_STATE_STOPPING;
		break;
	}
	return 0;
}

// returns time before next activations (in microseconds)
int device_get_action_interval(struct device *dev, struct timespec *ts)
{
	int result;

	switch(dev->state) {
	    case DEV_STATE_STOPPED:
		return WAKEUP_NEVER;
	    case DEV_STATE_STARTING:
		return WAKEUP_NOW;
	    default:
		break;
	}

	result = device_timespec_diff(&dev->next_action, ts);
	return (result > 0) ? result : WAKEUP_NOW;
}

int device_initialize(struct device *dev, const char *name, struct device_ops *ops, void *priv)
{
	if ((ops == NULL) || (priv == NULL))
		return -EINVAL;

	memset(dev, 0, sizeof(*dev));

	dev->name = strdup(name);
	if (dev->name == NULL)
		return -errno;

	dev->ops = ops;
	dev->priv = priv;
	dev->state = DEV_STATE_STOPPED;

	return 0;
}

int device_destroy(struct device *dev, int force)
{
	if (!force && (dev->state != DEV_STATE_STOPPED))
		return -EINVAL;

	if (dev->ops->destroy_priv != NULL)
		dev->ops->destroy_priv(dev);

	free((void*)dev->name);
	memset(dev, 0, sizeof(*dev));

	return 0;
}

int device_timespec_diff(struct timespec *a, struct timespec *b)
{
	return ((int)(a->tv_sec - b->tv_sec)) * 1000000 +
		((int)(a->tv_nsec - b->tv_nsec)) / 1000;
}

int device_timespec_cmp(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return +1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return +1;
	return 0;
}

void device_timespec_update(struct timespec *dst, struct timespec *src, int usec)
{
	if (dst != src) {
		dst->tv_sec = src->tv_sec;
		dst->tv_nsec = src->tv_nsec;
	}
	dst->tv_sec += usec / 1000000;
	dst->tv_nsec += (usec % 1000000) * 1000;

	if (dst->tv_nsec >= 1000000000) {
		dst->tv_sec += 1;
		dst->tv_nsec -= 1000000000;
	}
}
