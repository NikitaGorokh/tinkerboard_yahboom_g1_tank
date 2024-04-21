#ifndef CLIENT_SERVER_H
#define CLIENT_SERVER_H

#include <stdint.h>

#define HELLO_SERVER "hochu pogonyat"
#define HELLO_CLIENT "i tunchik tinkerboard"

struct tank_srv_info {
    int16_t right_speed, left_speed, sonic_distance;
    char sonik_servo_angle, camera_servo1_angle, camera_servo2_angle;
    char red, green, blue, buzzer;
};

#define TANK_SRV_MSG_TYPE_ALIVE_CHECK	'o'
#define TANK_SRV_MSG_TYPE_INFO_DATA	'k'

struct tank_srv_msg {
    char type;		// ALIVE_CHECK or INFO_DATA
    union {
	struct tank_srv_info info;
    };
};

//      name				cmd	button
#define TANK_CLNT_CMD_FORWARD		'w' // "w"
#define TANK_CLNT_CMD_RIGHT		'a' // "a"
#define TANK_CLNT_CMD_BACKWARD		's' // "s"
#define TANK_CLNT_CMD_LEFT		'd' // "d"
#define TANK_CLNT_CMD_STOP		'e' // "e"
#define TANK_CLNT_CMD_SONIC_RIGHT	'c' // "c"
#define TANK_CLNT_CMD_SONIC_CENTRE	'x' // "x"
#define TANK_CLNT_CMD_SONIC_LEFT	'z' // "z"
#define TANK_CLNT_CMD_CAMERA_UP		'r' // "\e[A"
#define TANK_CLNT_CMD_CAMERA_DOWN	't' // "\e[B"
#define TANK_CLNT_CMD_CAMERA_RIGHT	'y' // "\e[C"
#define TANK_CLNT_CMD_CAMERA_LEFT	'u' // "\e[D"
#define TANK_CLNT_CMD_CAMERA_CENTRE	'/' // "/"
#define TANK_CLNT_CMD_RED_LED		'1' // "1"
#define TANK_CLNT_CMD_GREEN_LED		'2' // "2"
#define TANK_CLNT_CMD_BLUE_LED		'3' // "3"
#define TANK_CLNT_CMD_BUZZER		'4' // "4"
#define TANK_CLNT_CMD_SONIC_MOD1	'5' // "5"
#define TANK_CLNT_CMD_SONIC_MOD0	'6' // "6"
#define TANK_CLNT_CMD_CONNECT_CHEK	'0'

struct tank_clnt_msg {
    char cmd;
};

#endif
