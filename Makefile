# SPDX-License-Identifier: GPL-2.0+
CC = gcc
CFLAGS = -Wall -W -g

all:	tank

tank:	unlock-io.o device.o track.o servo.o tank.o
	$(CC) $(CFLAGS) -o $@ $^ -lgpiod

clean:
	rm -f tank *.o
