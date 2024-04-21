#include <errno.h>
#include <string.h>
#include "sonic.h"
#include <stdio.h>

#define OFF	0
#define ON	1

enum sonic_state {
	SONIC_OFF=0,
	SEND_PULSE,
	WAIT_REPLY,
	REPLY_TIME,
	REST
};

struct sonic_priv {
	struct gpiod_line *in, *out;
	int distance[5], position, cnt, last_dist, mode;
	struct timespec start_time, start_signal;
	enum sonic_state state;
};

int calculate_distance(int time){
	int a=time*17/1000;
	if (a>450) return -1;
	return a;
	//Speed of sound = 340 m/s; time measures in usec
	//time=2*distance/speed; return cm
}

void sonic_change_mode (struct device *dev, int mode) {
	struct sonic_priv *priv = (struct sonic_priv *) dev->priv;
	priv->mode = mode;
};

int sonic_get_distance (struct device *dev) {
	struct sonic_priv *priv = (struct sonic_priv *) dev->priv;
	return priv->last_dist;
};

int sonic_start_request (struct device *dev) {
	struct sonic_priv *priv = (struct sonic_priv *) dev->priv;

	if (dev->state!=DEV_STATE_STOPPED) return -EINVAL;
	dev->state=DEV_STATE_STARTING;
	priv->state = SONIC_OFF;
	priv->position = 0;
	priv->cnt = 0;
	priv->last_dist = -1;
	return 0;
};


void sonic_destroy_priv (struct device *dev) {
	struct sonic_priv *priv = (struct sonic_priv *) dev->priv;
	gpiod_line_set_value (priv->out, OFF);
	free (priv);
};

void sonic_add_value(struct sonic_priv *priv, int value) {
	priv->distance[priv->position++]=value;
	if (priv->position >= 5) priv->position = 0;
	if (priv->cnt < 5) priv->cnt++;
	if (priv->cnt < 5) return;

	int distance=0, cnt=0;
	for (int i=0; i<5;i++){
		if (priv->distance[i]<0) continue;
		distance+=priv->distance[i];
		cnt++;
	}
	if (cnt >= 3) priv->last_dist=distance/cnt;
	else priv->last_dist = -1;
}

void sonic_timer_action (struct device *dev, struct timespec *ts) {
	struct sonic_priv *priv = (struct sonic_priv *) dev->priv;
	int period_time=device_timespec_diff(ts, &priv->start_time);
	if (dev->state==DEV_STATE_STOPPED) return;

	if (dev->state==DEV_STATE_STARTING) {
		dev->state=DEV_STATE_STARTED;
	};
	if (priv->state == SONIC_OFF) {
		if (dev->state==DEV_STATE_STOPPING || (priv->mode == 0 && priv->cnt == 5)) {
			dev->state=DEV_STATE_STOPPED;
			return;
		};
		priv->state = SEND_PULSE;
		gpiod_line_set_value (priv->out, ON);
		priv->start_time=*ts;
		device_timespec_update(&dev->next_action, ts, 10);
		return;
	};
	if (priv->state == SEND_PULSE){
		priv->state=WAIT_REPLY;
		gpiod_line_set_value (priv->out, OFF);
		device_timespec_update(&dev->next_action, ts,100);
		return;
	};
	if (priv->state == WAIT_REPLY){
		if(period_time >= SONIC_PERIOD / 2){
			sonic_add_value(priv, -1);
			priv->state = SONIC_OFF;
			device_timespec_update(&dev->next_action, &priv->start_time, SONIC_PERIOD);
			return;
		};
		if (gpiod_line_get_value(priv->in)==OFF){
			device_timespec_update(&dev->next_action, ts,15);
			return;
		}
		priv->state=REPLY_TIME;
		priv->start_signal=*ts;
		device_timespec_update(&dev->next_action, ts,100);
		return;
	}
	if (priv->state == REPLY_TIME){
		if (gpiod_line_get_value(priv->in)==OFF){
			sonic_add_value(priv, calculate_distance(device_timespec_diff(ts, &priv->start_signal)));
			priv->state = SONIC_OFF;
			device_timespec_update(&dev->next_action, &priv->start_time, SONIC_PERIOD);
			return;
		}
		if(period_time >= SONIC_PERIOD){
			sonic_add_value(priv, -1);
			priv->state = SONIC_OFF;
			return;
		}
		device_timespec_update(&dev->next_action, ts,15);
		return;
	};
};

struct device_ops sonic_ops={
	.start_request=sonic_start_request,
	.stop_request=device_stop_request,
	.timer_action=sonic_timer_action,
	.destroy_priv=sonic_destroy_priv
};

int sonic_init (struct device *dev, struct gpiod_line *in, struct gpiod_line *out)
{
	struct sonic_priv *priv;
	int ret;

	priv = (struct sonic_priv *)malloc(sizeof(struct sonic_priv));
	if (priv == NULL) return -errno;

	memset (priv, 0, sizeof(struct sonic_priv));

	priv->mode = 0;
	priv->last_dist = -1;

	priv->out = out;
	gpiod_line_request_output (priv->out, "sonic", OFF);

	priv->in = in;
	gpiod_line_request_input (priv->in, "sonic");

	ret = device_initialize (dev, "sonic", &sonic_ops, priv);
	if (ret != 0) {
		free (priv);
		return ret;
	};
	return 0;
}
