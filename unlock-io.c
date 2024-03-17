// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2016 Mikhail Kshevetskiy
 *
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@gmail.com>
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/select.h>
#include "unlock-io.h"

void kb_key_init(struct kb_key *kb){
    memset(kb, 0, sizeof(struct kb_key));
    kb->echo = 1;
};

void kb_key_echo(struct kb_key *kb, int enable){
	int		fd = fileno(stdin);
	struct termios	ttystate;

	tcgetattr(fd, &ttystate);
	if (enable) ttystate.c_lflag |= ECHO;
	else ttystate.c_lflag &= ~ECHO;
	tcsetattr(fd, TCSANOW, &ttystate);
	kb->echo = enable;
}

void kb_key_nonblock(struct kb_key *kb, int enable){
	int		fd = fileno(stdin);
	struct termios	ttystate;

	tcgetattr(fd, &ttystate);
	if (enable){
		ttystate.c_lflag &= ~ICANON;
		ttystate.c_cc[VMIN] = 1;
	}else{
		ttystate.c_lflag |= ICANON;
	}
	tcsetattr(fd, TCSANOW, &ttystate);
	kb->nonblock = enable;
}

int kb_key_len(struct kb_key *kb){
    unsigned	i;

    if (kb->buf_used == 0) return 0;
    if (kb->buf[0] != 0x1b) return 1;

    if (kb->buf_used == 1) return -1;
    if (kb->buf[1] == 'O'){
	if (kb->buf_used == 2) return -1;
	if (strchr("PQRS", kb->buf[2]) != NULL) return 3;
	return 1;
    }
    if (kb->buf[1] == '['){
	for(i = 2; i < kb->buf_used; i++){
	    if (((kb->buf[i] >= '0') && (kb->buf[i] <= '9')) || (kb->buf[i] == ';')) continue;
	    if (strchr("ABCDFHPQRS~", kb->buf[i]) != NULL) return i + 1;
	    return 1;
	}
	return -1;
    }
    return 1;
}

int kb_key_read(struct kb_key *kb, char *buf, int bufsize){
    struct timeval	tv;
    fd_set		fds;
    ssize_t		retval;
    unsigned		diff;

    if (kb->buf_used < sizeof(kb->buf)){
	FD_ZERO(&fds);
	FD_SET(fileno(stdin), &fds);
	memset(&tv, 0, sizeof(tv));
	select(fileno(stdin) + 1, &fds, NULL, NULL, &tv);
	if (FD_ISSET(fileno(stdin), &fds)){
	    retval = read(fileno(stdin), kb->buf + kb->buf_used, sizeof(kb->buf) - kb->buf_used);
	    if (retval > 0) kb->buf_used += retval;
	}
    }

    retval = kb_key_len(kb);
    if (retval < 0){
	if ((kb->unfinished.tv_sec == 0) && (kb->unfinished.tv_usec == 0)){
	    gettimeofday(&kb->unfinished, NULL);
	    goto no_key;
	}
	gettimeofday(&tv, NULL);
	diff = (tv.tv_sec - kb->unfinished.tv_sec) * 1000000 + (tv.tv_usec - kb->unfinished.tv_usec);
	if (diff < 20000) goto no_key;
	retval = 1;
    }

    memset(&kb->unfinished, 0, sizeof(kb->unfinished));
    if (retval == 0) goto no_key;
    if (buf != NULL){
	if (bufsize > retval){
	    memcpy(buf, kb->buf, retval);
	    buf[retval] = '\0';
	}else if (bufsize > 0){
	    *buf = '\0';
	}
    }
    kb->buf_used -= retval;
    memmove(kb->buf, kb->buf + retval, kb->buf_used);
    return 1;

  no_key:
    if ((buf != NULL) && (bufsize > 0)) *buf = '\0';
    return 0;
}
