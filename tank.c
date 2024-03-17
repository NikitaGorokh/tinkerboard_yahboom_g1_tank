// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024  Nikita Gorokhovatskiy,
 *			  (C) 2024  Andrey Kshevetskiy
 *
 * Authors:
 *	Nikita Gorokhovatskiy	<nikita.gorokhovatskiy@gmail.com>
 *	Andrey Kshevetskiy	<andrey.kshevetskiy@gmail.com>
 */

#include <time.h>
#include <unistd.h>
#include "device.h"
#include <gpiod.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "unlock-io.h"
#include "track.h"
#include "servo.h"

#define NUMBER_DEV	4

#define GPIOCHIP5	5
#define GPIOCHIP6	6
#define GPIOCHIP7	7
#define GPIOCHIP8	8

#define CHIP7_PWMB	22
#define CHIP7_BIN1	8
#define CHIP6_BIN2	1

#define CHIP7_PWMA	7
#define CHIP6_AIN1	4
#define CHIP6_AIN2	3

#define SERVO1_LINE 	10
#define SERVO2_LINE 	6
#define SERVO3_LINE 	8

#define RED_LINE	15
#define GREEN_LINE	14
#define BLUE_LINE	11

#define BUZZER_LINE	7

#define SERVO1_MIN	0
#define SERVO1_MAX	160
#define SERVO1_DEF	80

#define SERVO2_MIN	0
#define SERVO2_MAX	170
#define SERVO2_DEF	85

#define SERVO3_MIN	25
#define SERVO3_MAX	160
#define SERVO3_DEF	60

int sign (int n){
	if (n>0) return 1;
	if (n<0) return -1;
	return 0;
};

struct tanker {
	struct device dev[5];
	int dev_cnt;
	
	struct gpiod_line *red, *green, *blue, *buzzer;
};

int track_setup (struct device *dev, struct gpiod_chip *chip6, struct gpiod_chip *chip7)
{
	struct gpiod_line *pwmb, *bin1, *bin2;
	struct gpiod_line *pwma, *ain1, *ain2;
	pwmb = gpiod_chip_get_line(chip7, CHIP7_PWMB);
	if (!pwmb) {
		printf ("get track PWDB error\n");
		return -errno;
	};

	bin1 = gpiod_chip_get_line(chip7, CHIP7_BIN1);
	if (!bin1) {
		printf ("get track BIN1 error\n");
		return -errno;
	};

	bin2 = gpiod_chip_get_line(chip6, CHIP6_BIN2);
	if (!bin2) {
		printf ("get track BIN2 error\n");
		return -errno;
	};

	pwma = gpiod_chip_get_line(chip7, CHIP7_PWMA);
	if (!pwma) {
		printf ("get track PWDA error\n");
		return -errno;
	};

	ain1 = gpiod_chip_get_line(chip6, CHIP6_AIN1);
	if (!ain1) {
		printf ("get track AIN1 error\n");
		return -errno;
	};

	ain2 = gpiod_chip_get_line(chip6, CHIP6_AIN2);
	if (!ain2) {
		printf ("get track AIN2 error\n");
		return -errno;
	};

	return track_init(dev, pwmb, bin1, bin2, pwma, ain1, ain2);
}

int servo_setup (struct device *dev, int s_min, int s_max, int s_def, int s_line, struct gpiod_chip *chip)
{
	struct gpiod_line *line;
	line = gpiod_chip_get_line(chip, s_line);
	if (!line) {
		printf ("get servo line %d error\n", s_line);
		return -errno;
	};
	return angle_servo_init (dev, s_min, s_max, s_def, line);
}

void track_direction(char *x, struct device *dev){
	int r = track_get_speed_right (dev);
	int l = track_get_speed_left (dev);
	r=(r-sign(r)*TRACK_MINTIME)/TRACK_DELTA;
	l=(l-sign(l)*TRACK_MINTIME)/TRACK_DELTA;

	if (dev->state == DEV_STATE_STOPPED) dev->ops->start_request(dev);
	if(strcmp(x, "w")==0) {r=(r+l)/2+1; l=r;};
	if(strcmp(x, "s")==0) {r=(r+l)/2-1; l=r;};
	if(strcmp(x, "a")==0) {r+=1; l-=1;};
	if(strcmp(x, "d")==0) {r-=1; l+=1;};
	if(strcmp(x, "e")==0) {r=0; l=0;};
	
	if (abs(r)>((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA)) r=sign(r)*((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA);
	if (abs(l)>((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA)) l=sign(l)*((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA);
	
	r=sign(r)*(TRACK_MINTIME+TRACK_DELTA*abs(r));
	l=sign(l)*(TRACK_MINTIME+TRACK_DELTA*abs(l));
	
	track_set_speed (dev, r, l);
}

void servo_direction (char *x, struct device *dev) {
	int a = angle_get (dev);
	if ((strcmp(x, "z")==0) || (strcmp(x, "\e[D")==0) || (strcmp(x, "\e[A")==0)) a += 5;
	if ((strcmp(x, "c")==0) || (strcmp(x, "\e[C")==0) || (strcmp(x, "\e[B")==0)) a -= 5;
	if ((strcmp(x, "x")==0) || (strcmp(x, "/")==0)) a = angle_def(dev);
	angle_set (dev, a);
}

void led_set (struct gpiod_line *line) {
	gpiod_line_set_value (line, (gpiod_line_get_value(line)+1)%2);
}

void print_state(struct tanker *tank){
	printf ("\rtrack_power [%+04d%%, %+04d%%], sonic [%+03d, %03dm], camera [%+04d, %+04d], led [%c%c%c], buzzer [%s]",
			100*track_get_speed_left (&tank->dev[0])/TRACK_PERIOD, 100*track_get_speed_right (&tank->dev[0])/TRACK_PERIOD,
			angle_get (&tank->dev[1])-angle_def(&tank->dev[1]),0,
			angle_get (&tank->dev[2])-angle_def(&tank->dev[2]), angle_get (&tank->dev[3])-angle_def(&tank->dev[3]),
			gpiod_line_get_value(tank->red)==1?'R':'_', gpiod_line_get_value(tank->green)==1?'G':'_', 
			gpiod_line_get_value(tank->blue)==1?'B':'_', gpiod_line_get_value(tank->buzzer)==0?"PAIN":"____");
	fflush (stdout);
}

int main () {
	struct timespec ts;
	struct tanker tank;
	struct device *dev;
	int i, delay, ret, state=0;
	tank.dev_cnt=4;
	struct gpiod_chip *chip5, *chip6, *chip7, *chip8;
	struct kb_key kb;
	char x[10];
	
	chip5 = gpiod_chip_open_by_number (GPIOCHIP5);
	if (!chip5) {
		printf ("Open gpiochip%d error\n", GPIOCHIP5);
		return -errno;
	};

	chip6 = gpiod_chip_open_by_number (GPIOCHIP6);
	if (!chip6) {
		printf ("Open gpiochip%d error\n", GPIOCHIP6);
		return -errno;
	};

	chip7 = gpiod_chip_open_by_number (GPIOCHIP7);
	if (!chip7) {
		printf ("Open gpiochip%d error\n", GPIOCHIP7);
		return -errno;
	};
	
	chip8 = gpiod_chip_open_by_number (GPIOCHIP8);
	if (!chip8) {
		printf ("Open gpiochip%d error\n", GPIOCHIP8);
		return -errno;
	};

	tank.red = gpiod_chip_get_line (chip5, RED_LINE);
	if (!tank.red) {
		printf ("get tank.red line error\n");
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return -errno;
	};

	tank.green = gpiod_chip_get_line (chip5, GREEN_LINE);
	if (!tank.green) {
		printf ("get tank.green line error\n");
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return -errno;
	};

	tank.blue = gpiod_chip_get_line (chip5, BLUE_LINE);
	if (!tank.blue) {
		printf ("get tank.blue line error\n");
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return -errno;
	};

	tank.buzzer = gpiod_chip_get_line (chip8, BUZZER_LINE);
	if (!tank.buzzer) {
		printf ("get tank.buzzer line error\n");
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return -errno;
	};

	gpiod_line_request_output (tank.red, "LED_gpiod", 0);
	gpiod_line_request_output (tank.green, "LED_gpiod", 0);
	gpiod_line_request_output (tank.blue, "LED_gpiod", 0);

	gpiod_line_request_output (tank.buzzer, "LED_gpiod", 1);

	dev = &tank.dev[0];
	ret = track_setup(dev, chip6, chip7);
	if (ret!=0) {
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return ret;
	};
	dev->ops->start_request(dev);

	track_set_speed(dev, 0, 0);
	
	ret = servo_setup (&tank.dev[1], SERVO1_MIN, SERVO1_MAX, SERVO1_DEF, SERVO1_LINE, chip5);
	if (ret!=0) {
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return ret;
	};
	
	ret = servo_setup (&tank.dev[2], SERVO2_MIN, SERVO2_MAX, SERVO2_DEF, SERVO2_LINE, chip8);
	if (ret!=0) {
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return ret;
	};
	
	ret = servo_setup (&tank.dev[3], SERVO3_MIN, SERVO3_MAX, SERVO3_DEF, SERVO3_LINE, chip8);
	if (ret!=0) {
		gpiod_chip_close (chip5);
		gpiod_chip_close (chip6);
		gpiod_chip_close (chip7);
		gpiod_chip_close (chip8);
		return ret;
	};
	
	for (i=1; i<tank.dev_cnt; i++){
		dev = &tank.dev[i];
		dev->ops->start_request(dev);
	};
	
	kb_key_init(&kb);
	kb_key_echo(&kb, 0);
	kb_key_nonblock(&kb, 1);
	int exit=0;

	printf("BUTTONS_OPTIONS:\n"
		"__________________________________________________\n"
		"MOVEMENT:\n"
		" 'w'=forward_and_speedup 's'=backward_and_slowdown\n"
		" 'a'=turn_left             'd'=turn_right\n"
		"SONIC_ANGLE:\n"
		" 'z'=turn_left             'c'=turn_right\n"
		" 'x'=return_to_start_position\n"
		"CAMERA_ANGLE_arrow_keys:\n"
		" 'left'=turn_left          'right'=turn_right\n"
		" 'up'=turn_гз              'down'=turn_down\n"
		" '/'=return_to_start_position\n"
		"LED\n"
		" '1'=red     '2'=green     '3'=blue\n"
		" press key again to shutdown led\n"
		"BUZZER:\n"
		" '4'=updown\n"
		" press key again to shutdown buzzer\n"
		"EXIT:\n"
		" 'q'=complete_program\n"
		"__________________________________________________\n");
	print_state(&tank);

	while(1) {
               clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

				while(kb_key_read(&kb, x, sizeof(x))) {
					if ((strcmp(x, "w")==0) || (strcmp(x, "e")==0) || (strcmp(x, "a")==0)
					|| (strcmp(x, "s")==0) || (strcmp(x, "d")==0)) track_direction(x, &tank.dev[0]);
					if ((strcmp(x, "z")==0) || (strcmp(x, "x")==0) || (strcmp(x, "c")==0)) servo_direction(x, &tank.dev[1]);
					if ((strcmp(x, "\e[C")==0) || (strcmp(x, "/")==0) || (strcmp(x, "\e[D")==0)) servo_direction(x, &tank.dev[2]);
					if ((strcmp(x, "\e[A")==0) || (strcmp(x, "/")==0) || (strcmp(x, "\e[B")==0)) servo_direction(x, &tank.dev[3]);
					if (strcmp(x, "1")==0) led_set(tank.red);
					if (strcmp(x, "2")==0) led_set(tank.green);
					if (strcmp(x, "3")==0) led_set(tank.blue);
					if (strcmp(x, "4")==0) led_set(tank.buzzer);
					if(strcmp(x, "q")==0) exit=1;
					if ((strcmp(x, "w")==0) || (strcmp(x, "e")==0) || (strcmp(x, "a")==0)
							|| (strcmp(x, "s")==0) || (strcmp(x, "d")==0) || (strcmp(x, "z")==0)
							|| (strcmp(x, "x")==0) || (strcmp(x, "c")==0) || (strcmp(x, "\e[C")==0)
							|| (strcmp(x, "/")==0) || (strcmp(x, "\e[D")==0) || (strcmp(x, "\e[A")==0)
							|| (strcmp(x, "\e[B")==0) || (strcmp(x, "1")==0) || (strcmp(x, "2")==0)
							|| (strcmp(x, "3")==0) || (strcmp(x, "4")==0)) state=1;
				};
				if (exit==1) break;
				
				if (state == 1) {state=0; print_state(&tank);};

                delay = WAKEUP_NEVER;
                for(i = 0; i < tank.dev_cnt; i++) {
                        dev = &tank.dev[i];

                        int wakeup = device_get_action_interval(dev, &ts);
                        if (wakeup == WAKEUP_NOW) {
                                dev->ops->timer_action(dev, &ts);
                                wakeup = device_get_action_interval(dev, &ts);
                        }
                        if (wakeup <= WAKEUP_NEVER) continue;
                        if ((delay <= WAKEUP_NEVER) || (wakeup < delay)) delay = wakeup;
                }

                if (delay > WAKEUP_NOW) usleep(delay);
                if (delay <= WAKEUP_NEVER) usleep(10000);
	};

	gpiod_line_set_value (tank.red, 0);
	gpiod_line_set_value (tank.green, 0);
	gpiod_line_set_value (tank.blue, 0);
	gpiod_line_set_value (tank.buzzer, 1);

	kb_key_nonblock(&kb, 0);
	kb_key_echo(&kb, 1);
	for (i=0;i<tank.dev_cnt;i++){
		device_destroy(&tank.dev[i], 1);
	};
	
	printf("\n");

	return 0;
}
