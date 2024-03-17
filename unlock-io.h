// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyrights (C) 2016 Mikhail Kshevetskiy
 *
 * Author: Mikhail Kshevetskiy <mikhail.kshevetskiy@gmail.com>
 */
#ifndef __UNLOCK_IO_H__
#define __UNLOCK_IO_H__

#include <sys/time.h>

struct kb_key{
    char		buf[32];
    unsigned		buf_used;
    struct timeval	unfinished;
    int			echo;
    int			nonblock;
};

void	kb_key_init(struct kb_key *kb);
void	kb_key_echo(struct kb_key *kb, int enable);
void	kb_key_nonblock(struct kb_key *kb, int enable);
int	kb_key_len(struct kb_key *kb);
int	kb_key_read(struct kb_key *kb, char *buf, int bufsize);

#endif /* __UNLOCK_IO_H__ */
