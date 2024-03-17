// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024 Andrey Kshevetskiy
 *
 * Author: Andrey Kshevetskiy <andrey.kshevetskiy@gmail.com>
 */

#include <errno.h>
#include <string.h>
#include "servo.h"

#define OFF	0
#define ON	1

#define SERVO_PERIOD	20000
#define MAX_LOOPS		50

enum servo_state{
	SERVO_OFF=0,
	SERVO_ON
};

struct servo_priv {
	struct gpiod_line *out;
	int angle, next_angle, min_angle, max_angle, def_angle;
	int loops;
	enum servo_state state;
};

int servo_start_request (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;
	if (dev->state!=DEV_STATE_STOPPED) return -EINVAL;
	dev->state=DEV_STATE_STARTING;
	priv->state = SERVO_OFF;
	return 0;
}

void servo_destroy_priv (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;
	gpiod_line_set_value (priv->out, OFF);
	free (priv);
}

void servo_timer_action (struct device *dev, struct timespec *ts) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;
	
	if (dev->state==DEV_STATE_STOPPED) return;
	
	if (dev->state==DEV_STATE_STARTING) {
		dev->state=DEV_STATE_STARTED;
		priv->loops = MAX_LOOPS;
	};
	
	if (priv->state == SERVO_OFF) {
		if ((priv->loops == 0) || (dev->state==DEV_STATE_STOPPING)) {
			dev->state=DEV_STATE_STOPPED;
			priv->loops = 0;
			return;
		};
		if (priv->angle != priv->next_angle) {
			priv->angle = priv->next_angle;
			priv->loops = MAX_LOOPS;
		};
		priv->state = SERVO_ON;
		priv->loops--;
		gpiod_line_set_value (priv->out, ON);
		device_timespec_update(&dev->next_action, ts, priv->angle*11+500);
	} else {
		priv->state = SERVO_OFF;
		gpiod_line_set_value (priv->out, OFF);
		device_timespec_update(&dev->next_action, ts, SERVO_PERIOD-priv->angle*11-500);
	};
}

struct device_ops servo_ops={
	.start_request=servo_start_request,
	.stop_request=device_stop_request,
	.timer_action=servo_timer_action,
	.destroy_priv=servo_destroy_priv
};

int angle_servo_init (struct device *dev, int min_angle, int max_angle, int def_angle,  struct gpiod_line *out)
{
	struct servo_priv *priv;
	int ret;
	priv = (struct servo_priv *)malloc(sizeof(struct servo_priv));
	if (priv == NULL) return -errno;
	
	memset (priv, 0, sizeof(struct servo_priv));
	
	priv->min_angle = min_angle;
	priv->max_angle = max_angle;
	priv->def_angle = def_angle;
	priv->angle = def_angle;
	priv->next_angle = def_angle;
	
	priv->out = out;
	gpiod_line_request_output (priv->out, "angle_servo", OFF);
	
	ret = device_initialize (dev, "servo", &servo_ops, priv);
	if (ret != 0) {
		free (priv);
		return ret;
	};
	servo_start_request (dev);
	return 0;
}

int angle_get (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;
	
	return priv->next_angle;
}

int angle_min (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;

	return priv->min_angle;
}

int angle_max (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;

	return priv->max_angle;
}

int angle_def (struct device *dev) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;

	return priv->def_angle;
}

void angle_set (struct device *dev, int angle) {
	struct servo_priv *priv = (struct servo_priv *) dev->priv;
	
	if (angle<priv->min_angle) angle=priv->min_angle;
	if (angle>priv->max_angle) angle=priv->max_angle;
	priv->next_angle=angle;
	servo_start_request (dev);
}
