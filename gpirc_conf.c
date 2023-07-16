//SPDX-License-Identifier: GPL-2.1-or-later

/*

    Copyright (C) 2022 Cyril Hrubis <metan@ucw.cz>

 */

#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <widgets/gp_widgets.h>
#include <utils/gp_json.h>
#include <utils/gp_app_cfg.h>
#include <utils/gp_vec.h>
#include "gpirc_conf.h"

struct gpirc_conf gpirc_conf = {
	.port = 6667,
};

struct gp_json_struct chan_desc[] = {
	GP_JSON_SERDES_STR_DUP(struct gpirc_chan, chan, 0, 1024, "name"),
	GP_JSON_SERDES_STR_DUP(struct gpirc_chan, pass, GP_JSON_SERDES_OPTIONAL, 1024, "password"),
	{}
};

static void parse_channels(gp_json_reader *json, gp_json_val *val)
{
	GP_JSON_ARR_FOREACH(json, val) {
		struct gpirc_chan chan = {};

		if (gp_json_read_struct(json, val, chan_desc, &chan)) {
			gp_json_err(json, "Expected {\"name\": \"#chan-name\"} object");
			continue;
		}

		GP_VEC_APPEND(gpirc_conf.chans, chan);
	}
}

static struct gp_json_obj_attr conf_attrs[] = {
	GP_JSON_OBJ_ATTR("channels", GP_JSON_ARR),
	GP_JSON_OBJ_ATTR("nick", GP_JSON_STR),
	GP_JSON_OBJ_ATTR("port", GP_JSON_INT),
	GP_JSON_OBJ_ATTR("server", GP_JSON_STR),
};

static struct gp_json_obj conf_obj_filter = {
	.attrs = conf_attrs,
	.attr_cnt = GP_ARRAY_SIZE(conf_attrs),
};

enum conf_keys {
	CHANNELS,
	NICK,
	PORT,
	SERVER,
};

static char *get_user_name(void)
{
	struct passwd *pw;

	pw = getpwuid(getuid());
	if (!pw)
		return NULL;

	return strdup(pw->pw_name);
}

int gpirc_conf_load(gp_widget *status_log)
{
	char *conf_path;
	gp_json_reader *json;
	char buf[128];
	gp_json_val val = {
		.buf = buf,
		.buf_size = sizeof(buf),
	};

	gpirc_conf.chans = gp_vec_new(0, sizeof(struct gpirc_chan));
	if (!gpirc_conf.chans)
		return 1;

	conf_path = gp_app_cfg_path("gpirc", "config.json");
	if (!conf_path)
		return 1;

	json = gp_json_reader_load(conf_path);
	if (!json) {
		if (errno == ENOENT) {
			gp_widget_log_append(status_log, "Config file not present");
			gpirc_conf.nick = get_user_name();
			if (!gpirc_conf.nick)
				return 1;
			return 0;
		}

		gp_widget_log_append(status_log, "Failed to load config.json");
	}

	gp_widget_log_append(status_log, "Loading config file");

	json->err_print_priv = status_log;
	json->err_print = (void*)gp_widget_log_append;

	GP_JSON_OBJ_FILTER(json, &val, &conf_obj_filter, NULL) {
		switch (val.idx) {
		case CHANNELS:
			parse_channels(json, &val);
		break;
		case NICK:
			gpirc_conf.nick = strdup(val.val_str);
		break;
		case PORT:
			gpirc_conf.port = val.val_int;
		break;
		case SERVER:
			gpirc_conf.server = strdup(val.val_str);
		break;
		}
	}

	int err = gp_json_reader_err(json);
	gp_json_reader_finish(json);
	gp_json_reader_free(json);
	free(conf_path);

	if (!gpirc_conf.nick) {
		gpirc_conf.nick = get_user_name();
		if (!gpirc_conf.nick)
			return 1;
	}

	return err;
}

int gpirc_conf_conn_set(struct gpirc_conf *self, const char *server, int port)
{
	char *tmp = strdup(server);

	if (!tmp)
		return 1;

	free(self->server);

	self->server = tmp;
	if (port)
		self->port = port;

	return 0;
}

int gpirc_conf_nick_set(struct gpirc_conf *self, const char *nick)
{
	char *tmp = strdup(nick);

	if (!nick)
		return 1;

	free(self->nick);
	self->nick = tmp;

	return 0;
}
