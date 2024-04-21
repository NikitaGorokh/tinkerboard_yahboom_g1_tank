# SPDX-License-Identifier: GPL-2.0+
CC = gcc
CFLAGS = -Wall -W -g

all:	tank tcp-client

tank:	unlock-io.o device.o track.o servo.o tank.o sonic.o
	$(CC) $(CFLAGS) -o $@ $^ -lgpiod

tcp-client:	unlock-io.o tcp-client.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f tank tcp-client *.o