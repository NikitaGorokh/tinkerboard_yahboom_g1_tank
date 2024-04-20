// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2024  Nikita Gorokhovatskiy,
 *			  (C) 2024  Andrey Kshevetskiy
 *
 * Authors:
 *	Nikita Gorokhovatskiy	<nikita.gorokhovatskiy@gmail.com>
 *	Andrey Kshevetskiy	<andrey.kshevetskiy@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netdb.h>

#include "client_server.h"
#include "unlock-io.h"

struct server {
    int fd, handhake, cnt_byte;
    union {
	char buf[21];
	struct tank_srv_msg msg;
    };
};

int main(int argc, char *argv[]){
    int hellc = strlen(HELLO_CLIENT), hells = strlen(HELLO_SERVER);
    struct addrinfo	hints;
    struct addrinfo	*result, *rp;
    int			retval;

    struct server serv;

    struct kb_key kb;
    char x[10], c;

    serv.handhake = 0; serv.cnt_byte=0;
    memset (serv.buf, 0, sizeof(serv.buf));

    if (argc != 3) {
	fprintf(stderr, "Usage: %s host port\n", argv[0]);
	exit(EXIT_FAILURE);
    };

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;           /* Any protocol */

    retval = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (retval != 0) {
	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(retval));
	exit(EXIT_FAILURE);
    };

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully bind(2).
       If socket(2) (or connect(2)) fails, we (close the socket
       and) try the next address. */

    for (rp = result; rp != NULL; rp = rp->ai_next) {
	serv.fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	if (serv.fd == -1) continue;

	if (connect(serv.fd, rp->ai_addr, rp->ai_addrlen) != -1) break;
	close(serv.fd);
    };

    if (rp == NULL) {
	fprintf(stderr, "Could not connect\n");
	exit(EXIT_FAILURE);
    };

    freeaddrinfo(result);

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
	" 'q'=unconnect client\n"
	" 'p'=shutdown tank program"
	"__________________________________________________\n");

    while(1){
	fd_set	rfds;
	ssize_t	retval;
	FD_ZERO (&rfds);
	FD_SET (serv.fd, &rfds);
	struct timeval timeout = {0, 0};

	usleep(20000);

	if (serv.handhake == 0) {
	    retval = read (serv.fd, serv.buf+serv.cnt_byte,
			    hellc-serv.cnt_byte);
	    if (retval <= 0) {
		printf("read error: %s\n", strerror(errno));
		break;
	    };
	    serv.cnt_byte += retval;
	    if ((serv.cnt_byte == hellc)&&(strncmp (serv.buf, HELLO_CLIENT, hellc) == 0)) {
		retval = write(serv.fd, HELLO_SERVER, hells);
		if (retval != strlen(HELLO_SERVER)){
		    if (retval < 0) printf("write error: %s\n", strerror(errno));
		    break;
		};
		serv.handhake=1;
		serv.cnt_byte=0;
	    };
	} else {
	    retval = select (serv.fd+1, &rfds, NULL, NULL, &timeout);
	    if (retval == -1){
		fprintf(stderr, "Could not select, error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	    };
	    if (FD_ISSET(serv.fd, &rfds)) {
		retval = read (serv.fd, serv.buf+serv.cnt_byte,
				sizeof (struct tank_srv_msg)-serv.cnt_byte);
		if (retval <= 0) {
		    printf("read error: %s\n", strerror(errno));
		    break;
		};
		serv.cnt_byte+=retval;
		while (serv.cnt_byte > 0) {
		    switch (serv.msg.type){
			case TANK_SRV_MSG_TYPE_ALIVE_CHECK:
			    x[0]=TANK_CLNT_CMD_CONNECT_CHEK;
			    retval = write(serv.fd, x, 1);
			    if (retval != 1){
				if (retval < 0) printf("write error: %s\n", strerror(errno));
				goto endloop;
			    };
			    memmove (serv.buf, serv.buf+1, serv.cnt_byte-1);
			    serv.cnt_byte-=1;
			    break;

			case TANK_SRV_MSG_TYPE_INFO_DATA:
			    if (serv.cnt_byte < sizeof (struct tank_srv_msg)) goto no_data;
			    printf ("\rtrack_power [%+04d%%, %+04d%%], sonic [%+03d, %03dm], camera [%+04d, %+04d], led [%c%c%c], buzzer [%c]",
				    (int16_t)ntohs(serv.msg.info.left_speed),
				    (int16_t)ntohs(serv.msg.info.right_speed),
				    serv.msg.info.sonik_servo_angle, (int16_t)ntohs(serv.msg.info.sonic_distance),
				    serv.msg.info.camera_servo1_angle, serv.msg.info.camera_servo2_angle,
				    serv.msg.info.red, serv.msg.info.green, serv.msg.info.blue, serv.msg.info.buzzer);
			    fflush(stdout);
			    memmove (serv.buf, serv.buf + sizeof (struct tank_srv_msg),
					serv.cnt_byte - sizeof (struct tank_srv_msg));
			    serv.cnt_byte -= sizeof (struct tank_srv_msg);
			    break;

			default:
			    goto endloop;
		    };
		};
no_data:
	    };
	    while(kb_key_read(&kb, x, sizeof(x))) {
		if (strcmp(x, "w")==0)         c = TANK_CLNT_CMD_FORWARD;
		else if (strcmp(x, "a")==0)    c = TANK_CLNT_CMD_RIGHT;
		else if (strcmp(x, "s")==0)    c = TANK_CLNT_CMD_BACKWARD;
		else if (strcmp(x, "d")==0)    c = TANK_CLNT_CMD_LEFT;
		else if (strcmp(x, "e")==0)    c = TANK_CLNT_CMD_STOP;
		else if (strcmp(x, "c")==0)    c = TANK_CLNT_CMD_SONIC_RIGHT;
		else if (strcmp(x, "x")==0)    c = TANK_CLNT_CMD_SONIC_CENTRE;
		else if (strcmp(x, "z")==0)    c = TANK_CLNT_CMD_SONIC_LEFT;
		else if (strcmp(x, "\e[A")==0) c = TANK_CLNT_CMD_CAMERA_UP;
		else if (strcmp(x, "\e[B")==0) c = TANK_CLNT_CMD_CAMERA_DOWN;
		else if (strcmp(x, "\e[C")==0) c = TANK_CLNT_CMD_CAMERA_RIGHT;
		else if (strcmp(x, "\e[D")==0) c = TANK_CLNT_CMD_CAMERA_LEFT;
		else if (strcmp(x, "/")==0)    c = TANK_CLNT_CMD_CAMERA_CENTRE;
		else if (strcmp(x, "1")==0)    c = TANK_CLNT_CMD_RED_LED;
		else if (strcmp(x, "2")==0)    c = TANK_CLNT_CMD_GREEN_LED;
		else if (strcmp(x, "3")==0)    c = TANK_CLNT_CMD_BLUE_LED;
		else if (strcmp(x, "4")==0)    c = TANK_CLNT_CMD_BUZZER;
		else if (strcmp(x, "q")==0)    goto endloop;
		else c = '5';
		if (c!='5') {
		    retval = write(serv.fd, &c, 1);
		    if (retval != 1){
			if (retval < 0) printf("write error: %s\n", strerror(errno));
			goto endloop;
		    };
		};
	    };
	}
    };

endloop:
    kb_key_nonblock(&kb, 0);
    kb_key_echo(&kb, 1);
    close(serv.fd);
    printf("\n");
    return 0;
}
