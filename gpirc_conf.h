//SPDX-License-Identifier: GPL-2.1-or-later

/*

    Copyright (C) 2022 Cyril Hrubis <metan@ucw.cz>

 */

#ifndef GPIRC_CONF_H__
#define GPIRC_CONF_H__

#include <widgets/gp_widget_types.h>

struct gpirc_chan {
	char *chan;
	char *pass;
};

struct gpirc_conf {
	char *server;
	int port;
	char *nick;
	struct gpirc_chan *chans;
};

extern struct gpirc_conf gpirc_conf;

int gpirc_conf_load(gp_widget *status_log);

#endif /* GPIRC_CONF_H__ */
