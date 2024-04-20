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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>

#include "client_server.h"

#define MAX_CONNECTION	10


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

#define TIME_WAIT	30000000


struct tanker {
	struct device dev[5];
	int dev_cnt;
	
	struct gpiod_line *red, *green, *blue, *buzzer;
};

struct client{
	int fd;
	int handshake;
	int bytes;
	char buf[100];
	struct timespec last_check;
	int sucsess_check;
};

int sign (int n){
	if (n>0) return 1;
	if (n<0) return -1;
	return 0;
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

void track_direction(char cmd, struct device *dev){
	int r = track_get_speed_right (dev);
	int l = track_get_speed_left (dev);
	r=(r-sign(r)*TRACK_MINTIME)/TRACK_DELTA;
	l=(l-sign(l)*TRACK_MINTIME)/TRACK_DELTA;

	if (dev->state == DEV_STATE_STOPPED) dev->ops->start_request(dev);
	if(cmd==TANK_CLNT_CMD_FORWARD) {r=(r+l)/2+1; l=r;};
	if(cmd==TANK_CLNT_CMD_BACKWARD) {r=(r+l)/2-1; l=r;};
	if(cmd==TANK_CLNT_CMD_RIGHT) {r+=1; l-=1;};
	if(cmd==TANK_CLNT_CMD_LEFT) {r-=1; l+=1;};
	if(cmd==TANK_CLNT_CMD_STOP) {r=0; l=0;};
	
	if (abs(r)>((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA)) r=sign(r)*((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA);
	if (abs(l)>((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA)) l=sign(l)*((TRACK_PERIOD-TRACK_MINTIME)/TRACK_DELTA);
	
	r=sign(r)*(TRACK_MINTIME+TRACK_DELTA*abs(r));
	l=sign(l)*(TRACK_MINTIME+TRACK_DELTA*abs(l));
	
	track_set_speed (dev, r, l);
}

void servo_direction (char cmd, struct device *dev) {
	int a = angle_get (dev);
	switch(cmd){
		case TANK_CLNT_CMD_SONIC_LEFT:
		case TANK_CLNT_CMD_CAMERA_LEFT:
		case TANK_CLNT_CMD_CAMERA_UP:
			a += 5;
			break;
		case TANK_CLNT_CMD_SONIC_RIGHT:
		case TANK_CLNT_CMD_CAMERA_RIGHT:
		case TANK_CLNT_CMD_CAMERA_DOWN:
			a -= 5;
			break;
		case TANK_CLNT_CMD_SONIC_CENTRE:
		case TANK_CLNT_CMD_CAMERA_CENTRE:
			a = angle_def(dev);
	}
	angle_set (dev, a);
}

void led_set (struct gpiod_line *line) {
	gpiod_line_set_value (line, (gpiod_line_get_value(line)+1)%2);
}

int key_phess_handle(char cmd, struct tanker *tank){
	switch(cmd){
		case TANK_CLNT_CMD_FORWARD:
		case TANK_CLNT_CMD_BACKWARD:
		case TANK_CLNT_CMD_RIGHT:
		case TANK_CLNT_CMD_LEFT:
		case TANK_CLNT_CMD_STOP:
			track_direction(cmd, &tank->dev[0]);
			return 1;
		case TANK_CLNT_CMD_SONIC_RIGHT:
		case TANK_CLNT_CMD_SONIC_LEFT:
		case TANK_CLNT_CMD_SONIC_CENTRE:
			servo_direction(cmd, &tank->dev[1]);
			return 1;
		case TANK_CLNT_CMD_CAMERA_RIGHT:
		case TANK_CLNT_CMD_CAMERA_LEFT:
			servo_direction(cmd, &tank->dev[2]);
			return 1;
		case TANK_CLNT_CMD_CAMERA_UP:
		case TANK_CLNT_CMD_CAMERA_DOWN:
			servo_direction(cmd, &tank->dev[3]);
			return 1;
		case TANK_CLNT_CMD_CAMERA_CENTRE:
			servo_direction(cmd, &tank->dev[2]);
			servo_direction(cmd, &tank->dev[3]);
			return 1;
		case TANK_CLNT_CMD_RED_LED:
			led_set(tank->red);
			return 1;
		case TANK_CLNT_CMD_GREEN_LED:
			led_set(tank->green);
			return 1;
		case TANK_CLNT_CMD_BLUE_LED:
			led_set(tank->blue);
			return 1;
		case TANK_CLNT_CMD_BUZZER:
			led_set(tank->buzzer);
			return 1;
		default:
			return 0;
	}
}

void print_state(struct tanker *tank){
	printf ("\rtrack_power [%+04d%%, %+04d%%], sonic [%+03d, %03dm], camera [%+04d, %+04d], led [%c%c%c], buzzer [%c]",
			100*track_get_speed_left (&tank->dev[0])/TRACK_PERIOD, 100*track_get_speed_right (&tank->dev[0])/TRACK_PERIOD,
			angle_get (&tank->dev[1])-angle_def(&tank->dev[1]),0,
			angle_get (&tank->dev[2])-angle_def(&tank->dev[2]), angle_get (&tank->dev[3])-angle_def(&tank->dev[3]),
			gpiod_line_get_value(tank->red)==1?'R':'_', gpiod_line_get_value(tank->green)==1?'G':'_', 
			gpiod_line_get_value(tank->blue)==1?'B':'_', gpiod_line_get_value(tank->buzzer)==0?'P':'_');
	fflush (stdout);
}
int main (int argc, char *argv[]) {
	struct timespec ts;
	struct tanker tank;
	struct device *dev;
	int i, delay, ret, state=0;
	tank.dev_cnt=4;
	struct gpiod_chip *chip5, *chip6, *chip7, *chip8;
	struct kb_key kb;
	char x[10];

	int fd;
	struct client client[MAX_CONNECTION];
	struct tank_srv_info tank_state;
	struct tank_srv_msg tank_msg;

	struct addrinfo	hints;
	struct addrinfo	*result, *rp;
	int retval, reuse_addr;
	char alive_check=TANK_SRV_MSG_TYPE_ALIVE_CHECK;
	int client_cnt=0;

	struct timeval time_wait_select;


	if (argc != 2) {
		fprintf(stderr, "Usage: %s port\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* TCP socket */
	hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
	hints.ai_protocol = 0;           /* Any protocol */
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	retval = getaddrinfo(NULL, argv[1], &hints, &result);
	if (retval != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retval));
		exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	Try each address until we successfully bind(2).
	If socket(2) (or bind(2)) fails, we (close the socket
	and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
	fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	if (fd == -1) continue;

	reuse_addr=1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) != 0) goto error;

	if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
	error:
	close(fd);
    }

	if (rp == NULL) {
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);

	if (listen(fd, 5) != 0){
		fprintf(stderr, "Could not listen, error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (int i=0; i < MAX_CONNECTION;i++){
		client[i].fd=-1;
	}

	
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
	
	int exit_tank=0;

	tank_state.right_speed=htons(100*track_get_speed_right (&tank.dev[0])/TRACK_PERIOD);
	tank_state.left_speed=htons(100*track_get_speed_left (&tank.dev[0])/TRACK_PERIOD);
	tank_state.sonik_servo_angle=angle_get (&tank.dev[1])-angle_def(&tank.dev[1]);
	tank_state.camera_servo1_angle=angle_get (&tank.dev[2])-angle_def(&tank.dev[2]);
	tank_state.camera_servo2_angle=angle_get (&tank.dev[3])-angle_def(&tank.dev[3]);
	tank_state.red=gpiod_line_get_value(tank.red)==1?'R':'_';
	tank_state.green=gpiod_line_get_value(tank.green)==1?'G':'_';
	tank_state.blue=gpiod_line_get_value(tank.blue)==1?'B':'_';
	tank_state.buzzer=gpiod_line_get_value(tank.buzzer)==0?'P':'_';
	tank_msg.info=tank_state;
	kb_key_init(&kb);
	kb_key_echo(&kb, 0);
	kb_key_nonblock(&kb, 1);

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
		fd_set	rfds;
		int	i, retval, max;

		clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

		state = 0;
		while(kb_key_read(&kb, x, sizeof(x))){
			if (strlen(x) == 1) {
				if (x[0] == 'q') exit_tank=1;
				else state |= key_phess_handle(x[0], &tank);
			}else if (strcmp(x, "\e[A")==0) {
				state |= key_phess_handle(TANK_CLNT_CMD_CAMERA_UP, &tank);
			}else if (strcmp(x, "\e[B")==0) {
				state |= key_phess_handle(TANK_CLNT_CMD_CAMERA_DOWN, &tank);
			}else if (strcmp(x, "\e[C")==0) {
				state |= key_phess_handle(TANK_CLNT_CMD_CAMERA_RIGHT, &tank);
			}else if (strcmp(x, "\e[D")==0) {
				state |= key_phess_handle(TANK_CLNT_CMD_CAMERA_LEFT, &tank);
			}
		}

		FD_ZERO(&rfds);

		max = fd;
		FD_SET(fd, &rfds);
		for(i = 0; i < MAX_CONNECTION; i++){
			if (client[i].fd<0) continue;
			if (client[i].fd > max) max = client[i].fd;
			FD_SET(client[i].fd, &rfds);
		}

		time_wait_select.tv_sec=0;
		time_wait_select.tv_usec=0;
		retval = select(max + 1, &rfds, NULL, NULL, &time_wait_select);
		if (retval == -1){
			fprintf(stderr, "\nCould not select, error: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(fd, &rfds)){
			struct 	sockaddr_storage	peer_addr;
			socklen_t			peer_addr_len;
			char				host[NI_MAXHOST], service[NI_MAXSERV];
			int				fd1, none_client=-1;

			peer_addr_len = sizeof(peer_addr);
			fd1 = accept(fd, (struct sockaddr *) &peer_addr, &peer_addr_len);
			if (fd1 != -1){
				retval = getnameinfo((struct sockaddr *) &peer_addr,
						peer_addr_len, host, NI_MAXHOST,
						service, NI_MAXSERV, NI_NUMERICSERV);
				if (retval == 0) printf("\nReceived connection from %s:%s\n", host, service);
				else fprintf(stderr, "\ngetnameinfo: %s\n", gai_strerror(retval));

				for (int i=0; i<MAX_CONNECTION; i++){
					if (client[i].fd<0){
						none_client=i;
						break;
					};
				};
				if (none_client!=-1){
					int ret_val = write(fd1, HELLO_CLIENT, strlen(HELLO_CLIENT));
					if (ret_val!=strlen(HELLO_CLIENT)){
					printf("\nclose connection %d, can't send hello string, %s\n", none_client, strerror(errno));
					close(fd1);

					}else{
						client[none_client].fd = fd1;
						client[none_client].bytes=0;
						client[none_client].handshake=0;
					}

				}else{
					fprintf(stderr, "\nMax connections reached, drop new connection\n");
					close(fd1);
				}
			}
		}
		for(i = 0; i < MAX_CONNECTION; i++){
			ssize_t			bytes;

			if (!FD_ISSET(client[i].fd, &rfds)) continue;

			bytes = read(client[i].fd, client[i].buf+client[i].bytes, sizeof(client[i].buf)-client[i].bytes);
			if (bytes <= 0){
				if (bytes < 0){
					// error
					printf("\nread error: %s\n", strerror(errno));
				}
				printf("\nclose connection %d, bad read\n", i);
				close(client[i].fd);
				client[i].fd=-1;
				continue;
			}

			client[i].bytes+=bytes;

			if (client[i].handshake!=1){
				if (client[i].bytes<strlen(HELLO_SERVER)) continue;
				if (strncmp(client[i].buf, HELLO_SERVER, strlen(HELLO_SERVER))==0){
					client[i].handshake=1;
					client[i].bytes-=strlen(HELLO_SERVER);
					client_cnt+=1;
					tank_msg.info=tank_state;
					tank_msg.type=TANK_SRV_MSG_TYPE_INFO_DATA;
					int ret_val = write(client[i].fd, &tank_msg, sizeof(tank_msg));
					if (ret_val!=sizeof(tank_msg)){
						printf("\nclose connection %d, can't send status_tank, %s\n", i, strerror(errno));
						close(client[i].fd);
						client[i].fd=-1;
						client_cnt-=1;
					}
					client[i].last_check=ts;
					client[i].sucsess_check=1;
					if (client[i].bytes>0)
						memmove(client[i].buf, client[i].buf+strlen(HELLO_SERVER), client[i].bytes);

				}else{
					close(client[i].fd);
					client[i].fd=-1;
					printf("\nwrong client[%d] hello string\n", i);
					continue;
				}
			};


			for(int j=0; j<client[i].bytes; j++){
				if (client[i].buf[j]==TANK_CLNT_CMD_CONNECT_CHEK){
					client[i].sucsess_check=1;
				}else if (key_phess_handle(client[i].buf[j], &tank) != 0){
					state = 1;
				}else {
					close(client[i].fd);
					client[i].fd=-1;
					client_cnt-=1;
					printf("\nwrong client[%d] comand\n", i);
					continue;
				}
			}
			client[i].bytes=0;
		};

		if (exit_tank==1) break;
		for(i=0; i<MAX_CONNECTION; i++){
			if (client[i].fd==-1 || client[i].handshake!=1) continue;
			if (device_timespec_diff(&ts, &client[i].last_check)>=TIME_WAIT){
				if(client[i].sucsess_check==1){
					int ret_val = write(client[i].fd, &alive_check, sizeof(alive_check));
					if (ret_val!=sizeof(alive_check)){
						printf("\nclose connection %d, can't ping, %s\n", i, strerror(errno));
						close(client[i].fd);
						client[i].fd=-1;
						client_cnt-=1;
					}
					client[i].sucsess_check=0;
					client[i].last_check=ts;
					continue;
				}
				printf("\nclose connection %d, timeout happens\n", i);
				close(client[i].fd);
				client[i].fd=-1;
				if (client_cnt<0){
					char stop=TANK_CLNT_CMD_STOP;
					track_direction(stop, &tank.dev[0]);
				}
			}
			if (state == 1) {
				tank_state.sonic_distance=htons(0);
				tank_state.right_speed=htons(100*track_get_speed_right (&tank.dev[0])/TRACK_PERIOD);
				tank_state.left_speed=htons(100*track_get_speed_left (&tank.dev[0])/TRACK_PERIOD);
				tank_state.sonik_servo_angle=angle_get (&tank.dev[1])-angle_def(&tank.dev[1]);
				tank_state.camera_servo1_angle=angle_get (&tank.dev[2])-angle_def(&tank.dev[2]);
				tank_state.camera_servo2_angle=angle_get (&tank.dev[3])-angle_def(&tank.dev[3]);
				tank_state.red=gpiod_line_get_value(tank.red)==1?'R':'_';
				tank_state.green=gpiod_line_get_value(tank.green)==1?'G':'_';
				tank_state.blue=gpiod_line_get_value(tank.blue)==1?'B':'_';
				tank_state.buzzer=gpiod_line_get_value(tank.buzzer)==0?'P':'_';
				tank_msg.info=tank_state;
				tank_msg.type=TANK_SRV_MSG_TYPE_INFO_DATA;
				int ret_val = write(client[i].fd, &tank_msg, sizeof(tank_msg));
				if (ret_val!=sizeof(tank_msg)){
					printf("\nclose connection %d, can't send status_tank, %s\n", i, strerror(errno));
					close(client[i].fd);
					client[i].fd=-1;
					client_cnt-=1;
					continue;
				}
			};
		}
		if (state == 1) print_state(&tank);

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

	for (i=0;i<MAX_CONNECTION;i++){
		if (client[i].fd==-1) continue;
		close(client[i].fd);
	}
	close(fd);
	kb_key_nonblock(&kb, 0);
	kb_key_echo(&kb, 1);

	gpiod_line_set_value (tank.red, 0);
	gpiod_line_set_value (tank.green, 0);
	gpiod_line_set_value (tank.blue, 0);
	gpiod_line_set_value (tank.buzzer, 1);

	for (i=0;i<tank.dev_cnt;i++){
		device_destroy(&tank.dev[i], 1);
	};

	printf("\n");

	return 0;
}
