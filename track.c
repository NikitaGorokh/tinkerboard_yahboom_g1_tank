// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024 Nikita Gorokhovatskiy
 *
 * Author: Nikita Gorohovatskiy <nikita.gorokhovatskiy@gmail.com>
 */

#include <errno.h>
#include <string.h>
#include "track.h"

#define OFF		0
#define ON		1

int min(int x, int y){
	return x<y?x:y;
}
int max(int x, int y){
	return x>y?x:y;
}


enum track_state{
	TRACK_OFF=0,
	TRACK_ON
};

struct track_manage {
	struct gpiod_line *pwm, *in1, *in2;
	enum track_state state;
	int worktime, next_worktime;
};

struct track_prive{
	struct track_manage right;
	struct track_manage left;
};

int track_start_request (struct device *dev) {
	struct track_prive *priv = (struct track_prive *) dev->priv;
	if (dev->state!=DEV_STATE_STOPPED) return -EINVAL;
	dev->state=DEV_STATE_STARTING;
	priv->right.state = TRACK_OFF;
	priv->left.state = TRACK_OFF;
	return 0;
}

void track_control (struct track_manage *track, enum track_state state) {
	gpiod_line_set_value (track->in1, track->worktime < 0 ? state : OFF);
	gpiod_line_set_value (track->in2, track->worktime > 0 ? state : OFF);
	gpiod_line_set_value (track->pwm, track->worktime != 0 ? state : OFF);
	track->state=state;
};

void track_timer_action (struct device *dev, struct timespec *ts) {
	struct track_prive *priv = (struct track_prive *) dev->priv;
	int delay;
	int time_r = abs(priv->right.worktime);
	int time_l = abs(priv->left.worktime);

	if (dev->state==DEV_STATE_STOPPED) return;

	if ((dev->state==DEV_STATE_STOPPING) && priv->right.state==TRACK_OFF && priv->left.state==TRACK_OFF){
		dev->state=DEV_STATE_STOPPED;
		return;
	} ;

	if (dev->state==DEV_STATE_STARTING) dev->state=DEV_STATE_STARTED;

	if (priv->right.state==TRACK_OFF && priv->left.state==TRACK_OFF){
		if (priv->right.worktime != priv->right.next_worktime) {
			priv->right.worktime = priv->right.next_worktime;
			time_r = abs(priv->right.worktime);
		};
		if (priv->left.worktime != priv->left.next_worktime) {
			priv->left.worktime = priv->left.next_worktime;
			time_l = abs(priv->left.worktime);
		};
		if(time_r==0 && time_l==0){
			dev->state=DEV_STATE_STOPPED;
			return;
		};

		if(time_r!=0) track_control(&priv->right, TRACK_ON);
		if(time_l!=0) track_control(&priv->left, TRACK_ON);
		delay=min(time_r, time_l);
		if(delay==0) delay=max(time_r, time_l);
		device_timespec_update(&dev->next_action, ts, delay);
		return;
	};

	if (priv->right.state==TRACK_ON && priv->left.state==TRACK_ON){
		delay=min(time_r, time_l);
		if (time_r==delay) track_control(&priv->right, TRACK_OFF);
		if (time_l==delay) track_control(&priv->left, TRACK_OFF);
		delay=(time_r==time_l) ? TRACK_PERIOD-delay:abs(time_r-time_l);
		device_timespec_update(&dev->next_action, ts, delay);
		return;
	};

	if (priv->right.state==TRACK_ON && priv->left.state==TRACK_OFF){
		track_control(&priv->right, TRACK_OFF);
		delay=TRACK_PERIOD-time_r;
		device_timespec_update(&dev->next_action, ts, delay);
		return;
	};

	if (priv->right.state==TRACK_OFF && priv->left.state==TRACK_ON){
		track_control(&priv->left, TRACK_OFF);
		delay=TRACK_PERIOD-time_l;
		device_timespec_update(&dev->next_action, ts, delay);
		return;
	};
}

void track_destroy_priv (struct device *dev) {
	struct track_prive *priv = (struct track_prive *) dev->priv;
	track_control(&priv->right, TRACK_OFF);
	track_control(&priv->left, TRACK_OFF);
	free(priv);
}

struct device_ops track_ops={
	.start_request=track_start_request,
	.stop_request=device_stop_request,
	.timer_action=track_timer_action,
	.destroy_priv=track_destroy_priv
};


int track_init (struct device *dev,
				struct gpiod_line *pwmb, struct gpiod_line *bin1, struct gpiod_line *bin2,
				struct gpiod_line *pwma, struct gpiod_line *ain1, struct gpiod_line *ain2)
{
	struct track_prive *priv;
	int ret;

	priv = (struct track_prive *)malloc(sizeof(struct track_prive));
	if (priv == NULL) return -errno;

	memset (priv, 0, sizeof(struct track_prive));
	
	priv->right.pwm = pwmb;
	priv->right.in1 = bin1;
	priv->right.in2 = bin2;
	priv->left.pwm = pwma;
	priv->left.in1 = ain1;
	priv->left.in2 = ain2;

	gpiod_line_request_output (priv->right.pwm, "track_gpiod", OFF);
	gpiod_line_request_output (priv->right.in1, "track_gpiod", OFF);
	gpiod_line_request_output (priv->right.in2, "track_gpiod", OFF);

	gpiod_line_request_output (priv->left.pwm, "track_gpiod", OFF);
	gpiod_line_request_output (priv->left.in1, "track_gpiod", OFF);
	gpiod_line_request_output (priv->left.in2, "track_gpiod", OFF);

	ret = device_initialize (dev, "track", &track_ops, priv);
	if (ret != 0) {
		free (priv);
		return ret;
	};
	return 0;
}

void track_set_speed(struct device *dev, int workload_right, int workload_left)
{
	struct track_prive *priv = (struct track_prive *) dev->priv;

	priv->right.next_worktime=workload_right;
	priv->left.next_worktime=workload_left;
}

int track_get_speed_right (struct device *dev)
{
	struct track_prive *priv = (struct track_prive *) dev->priv;

	return priv->right.next_worktime;
}
int track_get_speed_left (struct device *dev)
{
	struct track_prive *priv = (struct track_prive *) dev->priv;

	return priv->left.next_worktime;
}
