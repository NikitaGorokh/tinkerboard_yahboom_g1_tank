// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2023-2024 Mikhail Kshevetskiy
 *
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@gmail.com>
 */
#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <time.h>

#define WAKEUP_NEVER	-1
#define WAKEUP_NOW	0

enum device_state {
	DEV_STATE_STOPPED,
	DEV_STATE_STARTING,
	DEV_STATE_STARTED,
	DEV_STATE_STOPPING,
};

struct device {
	const char		*name;

	struct device_ops	*ops;
	void			*priv;

	enum device_state	state;
	struct timespec		next_action;
};

struct device_ops {
	int	(*start_request)(struct device *dev);
	int	(*stop_request)(struct device *dev);
	void	(*timer_action)(struct device *dev, struct timespec *ts);
	void	(*destroy_priv)(struct device *dev);
};

int  device_stop_request(struct device *dev);

// returns time before next activations (in microseconds)
int  device_get_action_interval(struct device *dev, struct timespec *ts);

int  device_initialize(struct device *dev, const char *name, struct device_ops *ops, void *priv);
int  device_destroy(struct device *dev, int force);

int  device_timespec_diff(struct timespec *a, struct timespec *b);
int  device_timespec_cmp(struct timespec *a, struct timespec *b);
void device_timespec_update(struct timespec *dst, struct timespec *src, int usec);

#endif
