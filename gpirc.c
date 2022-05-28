//SPDX-License-Identifier: GPL-2.1-or-later

/*

    Copyright (C) 2022 Cyril Hrubis <metan@ucw.cz>

 */

#include <pwd.h>
#include <libircclient/libircclient.h>
#include <libircclient/libirc_rfcnumeric.h>
#include <utils/gp_vec.h>
#include <utils/gp_vec_str.h>
#include <widgets/gp_widgets.h>

static irc_session_t *irc_session;
static gp_widget *status_log;
static gp_widget *channel_tabs;
static gp_widget *topic;

static gp_htable *channels_map;

struct channel {
	gp_widget *channel_log;
	char *name;
	char *topic;
	//FIXME Hash table? Trie?
	char **nicks;
};

static struct irc_conf {
	char *server;
	int port;
	char *channel;
	char *nick;
} irc_conf = {
	.server = "irc.libera.chat",
	.port = 6667,
	.channel = "#test-channel",
};

static void status_log_append(const char *msg)
{
	gp_widget_log_append(status_log, msg);
}

static void status_log_appends(const char *msgs[], unsigned int cnt)
{
	char *msg = gp_vec_str_new();
	unsigned int i;

	if (!msg)
		return;

	for (i = 0; i < cnt; i++) {
		if (i)
			GP_VEC_STR_APPEND(msg, " ");
		GP_VEC_STR_APPEND(msg, msgs[i]);
	}

	status_log_append(msg);

	gp_vec_free(msg);
}

static void status_log_printf(const char *fmt, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	gp_widget_log_append(status_log, buf);
}

static int channels_init(void)
{
	channels_map = gp_htable_new(0, 0);

	return !channels_map;
}

static void channels_add(const char *chan_name)
{
	gp_widget *channel_log;
	struct channel *channel;

	channel = malloc(sizeof(struct channel));
	if (!channel)
		goto err0;

	channel->name = strdup(chan_name);
	if (!channel->name)
		goto err1;

	channel->nicks = gp_vec_new(0, sizeof(char **));
	if (!channel->nicks)
		goto err2;

	channel_log = gp_widget_log_new(GP_TATTR_MONO, 80, 25);
	if (!channel_log)
		goto err3;

	channel->topic = NULL;


	channel->channel_log = channel_log;
	channel_log->priv = channel;
	gp_htable_put(channels_map, channel, channel->name);
	channel_log->align = GP_FILL;
	gp_widget_tabs_tab_append(channel_tabs, chan_name, channel_log);

	return;
err3:
	gp_vec_free(channel->nicks);
err2:
	free(channel->name);
err1:
	free(channel);
err0:
	gp_widget_log_append(status_log, "Allocation failure");
}

static void channels_rem(gp_widget *channel_log)
{
	struct channel *channel = channel_log->priv;

	irc_cmd_part(irc_session, channel->name);

	gp_widget_tabs_tab_del_by_child(channel_tabs, channel_log);

	gp_htable_rem(channels_map, channel->name);

	free(channel->name);
	free(channel);
}

static struct channel *chan_by_name(const char *chan_name)
{
	struct channel *channel = gp_htable_get(channels_map, chan_name);

	if (!channel)
		status_log_printf("Channel '%s' does not exist!", chan_name);

	return channel;
}

static void channels_append(const char *chan_name, const char *msg)
{
	struct channel *chan = chan_by_name(chan_name);

	if (chan)
		gp_widget_log_append(chan->channel_log, msg);
}

static void channels_printf(const char *chan_name, const char *fmt, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	channels_append(chan_name, buf);
}

static gp_widget *channels_active(void)
{
	return gp_widget_tabs_active_child_get(channel_tabs);
}

static int channels_is_active(gp_widget *self)
{
	return channels_active() == self;
}

static int channels_is_status_log(gp_widget *self)
{
	return self == status_log;
}

static void channels_join(const char *name)
{
	status_log_printf("Joining channel '%s'", name);

	channels_add(name);

	irc_cmd_join(irc_session, name, 0);
}

static void chan_add_nick(const char *chan_name, const char *nick)
{
	struct channel *chan = chan_by_name(chan_name);

	if (!chan)
		return;

	GP_VEC_APPEND(chan->nicks, strdup(nick));
}

static void chan_add_nicks(const char *chan_name, const char *nicks)
{
	struct channel *chan = chan_by_name(chan_name);

	if (!chan)
		return;

	for (;;) {
		size_t nick_len = 0;

		while (nicks[nick_len] && nicks[nick_len] != ' ')
			nick_len++;

		if (!nick_len)
			return;

		GP_VEC_APPEND(chan->nicks, strndup(nicks, nick_len));

		while (nicks[nick_len] && nicks[nick_len] == ' ')
			nick_len++;

		nicks += nick_len;
	}
}

static void chan_rem_nick(const char *chan_name, const char *nick)
{
	struct channel *chan = chan_by_name(chan_name);

	if (!chan)
		return;

	//TODO
}

static void chan_print_nicks(const char *chan_name)
{
	struct channel *chan = chan_by_name(chan_name);

	if (!chan)
		return;

	channels_printf(chan_name, "[Users %s]", chan_name);

	char *nicks = gp_vec_str_new();

	if (!nicks)
		return;

	int first = 1;

	GP_VEC_FOREACH(chan->nicks, char *, nick) {
		if (!first)
			GP_VEC_STR_APPEND(nicks, " ");
		first = 0;
		GP_VEC_STR_APPEND(nicks, *nick);
	}

	channels_printf(chan_name, "[%s]", nicks);

	gp_vec_free(nicks);
}

static void set_topic_label(const char *topic_str)
{
	if (!topic)
		return;

	if (topic_str)
		gp_widget_label_set(topic, topic_str);
	else
		gp_widget_label_set(topic, "(none)");
}

static int channels_on_event(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	if (ev->sub_type != GP_WIDGET_TABS_ACTIVATED)
		return 0;

	gp_widget *active_child = channels_active();

	if (channels_is_status_log(active_child)) {
		set_topic_label("gpirc 1.0");
		return 1;
	}

	struct channel *channel = active_child->priv;

	set_topic_label(channel->topic);

	return 1;
}

static void event_connect(irc_session_t *session, const char *event,
                          const char *origin, const char **params,
                          unsigned int count)
{
	struct irc_conf *irc_conf = irc_get_ctx(session);

	(void) event;
	(void) origin;
	(void) params;
	(void) count;

	channels_join(irc_conf->channel);
}

static void event_join(irc_session_t *session, const char *event,
                       const char *origin, const char **params,
                       unsigned int count)
{
	char nick[128];

	(void) session;
	(void) event;

	if (count < 1)
		return;

	irc_target_get_nick(origin, nick, sizeof(nick));

	channels_printf(params[0], "%s [%s] has joined %s", nick, origin, params[0]);

	if (strcmp(nick, irc_conf.nick))
		chan_add_nick(params[0], nick);
}

static void event_part(irc_session_t *session, const char *event,
                       const char *origin, const char **params,
                       unsigned int count)
{
	char nick[128];

	(void) session;
	(void) event;

	if (count < 1)
		return;

	irc_target_get_nick(origin, nick, sizeof(nick));

	channels_printf(params[0], "%s [%s] has quit [Connection closed]", nick, origin);

	chan_rem_nick(params[0], nick);
}

static void event_nick(irc_session_t *session, const char *event,
                       const char *origin, const char **params,
                       unsigned int count)
{
	char nick[128];

	(void) session;
	(void) event;

	if (count < 1)
		return;

	irc_target_get_nick(origin, nick, sizeof(nick));

	//TODO: go over channels and rename nick
	printf("User %s changed nick to %s\n", nick, params[0]);
}

static void event_channel(irc_session_t *session, const char *event,
                          const char *origin, const char **params,
                          unsigned int count)
{
	char nick[128];

	(void) session;
	(void) event;

	if (count != 2)
		return;

	irc_target_get_nick(origin, nick, sizeof(nick));

	channels_printf(params[0], "<%s> %s", nick, params[1]);
}

static void chan_set_topic(const char *chan_name, const char *topic)
{
	struct channel *channel = gp_htable_get(channels_map, chan_name);
	if (!channel)
		return;

	free(channel->topic);
	channel->topic = strdup(topic);

	if (channels_is_active(channel->channel_log))
		set_topic_label(channel->topic);
}

static void event_topic(irc_session_t *session, const char *event,
                        const char *origin, const char **params,
			unsigned int count)
{
	(void) origin;
	(void) session;
	(void) event;
	char nick[128];

	if (count != 2)
		return;

	chan_set_topic(params[0], params[1]);

	irc_target_get_nick(origin, nick, sizeof(nick));

	channels_printf(params[0], "%s changed topic to '%s'", nick, params[1]);
}

static uint32_t poll_irc(gp_timer *self)
{
	fd_set in_set;
	fd_set out_set;
	int maxfd;
	struct timeval t = {};

	(void) self;

	FD_ZERO(&in_set);
	FD_ZERO(&out_set);
	irc_add_select_descriptors(irc_session, &in_set, &out_set, &maxfd);
	if (select(maxfd+1, &in_set, &out_set, NULL, &t) <= 0)
		return 0;
	irc_process_select_descriptors(irc_session, &in_set, &out_set);

	return 0;
}

static gp_timer poll_timer = {
	.period = 100,
	.callback = poll_irc,
	.id = "Poll IRC",
};

static void do_connect(void)
{
	int ret;

	status_log_printf("Connecting to %s port %i", irc_conf.server, irc_conf.port);

	ret = irc_connect(irc_session, irc_conf.server, irc_conf.port, 0, irc_conf.nick, 0, 0);
	if (!ret) {
		gp_widgets_timer_ins(&poll_timer);
		return;
	}

	gp_widget_log_append(status_log, "connection failed");
}

static int str_append(char **str, const char *suf)
{
	size_t str_len = strlen(*str);
	size_t suf_len = strlen(suf);
	char *ret = malloc(str_len + suf_len + 1);

	if (!ret)
		return 1;

	strcpy(ret, *str);
	strcpy(ret + str_len, suf);
	ret[str_len + suf_len] = 0;

	free(*str);
	*str = ret;

	return 0;
}

static void retry_with_new_nick(void)
{
	if (str_append(&irc_conf.nick, "_"))
		return;

	irc_cmd_nick(irc_session, irc_conf.nick);
}

static void event_numeric(irc_session_t *session, unsigned int event,
                          const char *origin, const char **params,
                          unsigned int count)
{
	(void)origin;
	(void)session;

	switch (event) {
	case LIBIRC_RFC_RPL_MOTD:
	case LIBIRC_RFC_RPL_WELCOME:
	case LIBIRC_RFC_RPL_YOURHOST:
	case LIBIRC_RFC_RPL_CREATED:
	case LIBIRC_RFC_RPL_ENDOFMOTD:
	case LIBIRC_RFC_RPL_MOTDSTART:
	case LIBIRC_RFC_RPL_LUSERCLIENT:
	case LIBIRC_RFC_RPL_LUSERME:
	case LIBIRC_RFC_RPL_LUSEROP:
	case LIBIRC_RFC_RPL_LUSERUNKNOWN:
	case LIBIRC_RFC_RPL_LUSERCHANNELS:
	/* Highest connection count */
	case 250:
	/* Current local users */
	case 265:
	/* Current global users */
	case 266:
	/* Displayed host */
	case 396:
		if (count == 2)
			status_log_append(params[1]);

		if (count >= 3)
			status_log_printf("%s %s", params[1], params[2]);
	break;
	case LIBIRC_RFC_RPL_BOUNCE:
	case LIBIRC_RFC_RPL_MYINFO:
		status_log_appends(params + 1, count - 1);
	break;
	case LIBIRC_RFC_RPL_ENDOFNAMES:
		chan_print_nicks(params[1]);
	break;
	case LIBIRC_RFC_RPL_NAMREPLY:
		chan_add_nicks(params[2], params[3]);
	break;
	case LIBIRC_RFC_RPL_NOTOPIC:
		printf("NOTOPIC %s", params[0]);
	break;
	case LIBIRC_RFC_RPL_TOPIC:
		if (count < 3)
			return;
		chan_set_topic(params[1], params[2]);
		channels_printf(params[1], "Topic set by %s: %s", params[0], params[2]);
	break;
	case LIBIRC_RFC_ERR_CHANOPRIVSNEEDED:
		channels_printf(params[1], "%s %s", params[1], params[2]);
	break;
	case LIBIRC_RFC_ERR_NICKNAMEINUSE:
		if (count >= 2)
			status_log_printf("Your nick %s is already in use", params[1]);
		retry_with_new_nick();
	break;
	default:
		status_log_printf("Unhandled event %i\n", event);
		printf("Unhandled event %i\n", event);
	break;
	}
}

static void cmd_quit(gp_widget *self, const char *pars)
{
	if (pars[0]) {
		gp_widget_log_append(self, "/quit command invalid parameters");
		return;
	}

	gp_widgets_exit(0);
}

static void cmd_wc(gp_widget *self, const char *pars)
{
	if (pars[0]) {
		gp_widget_log_append(self, "/wc command invalid parameters");
		return;
	}

	channels_rem(self);
}

static void cmd_join(gp_widget *self, const char *pars)
{
	if (!pars[0]) {
		gp_widget_log_append(self, "/join requires parameter");
		return;
	}

	channels_join(pars);
}

static void cmd_topic(gp_widget *self, const char *pars)
{
	struct channel *channel = self->priv;

	if (!pars[0]) {
		gp_widget_log_append(self, "/topic requires parameter");
		return;
	}

	irc_cmd_topic(irc_session, channel->name, pars);
}

static const char *help[] = {
	" /help       - Prints this help",
	" /join #chan - Joins channel #chan",
	" /quit       - Quits",
	" /topic      - Sets channel topic",
	" /wc         - Closes this window"
};

static void cmd_help(gp_widget *self, const char *pars)
{
	size_t i;

	(void) pars;

	for (i = 0; i < GP_ARRAY_SIZE(help); i++)
		gp_widget_log_append(self, help[i]);
}

static struct cmd {
	const char *cmd;
	void (*cmd_run)(gp_widget *self, const char *pars);
} cmds[] = {
	{"help", cmd_help},
	{"join", cmd_join},
	{"quit", cmd_quit},
	{"topic", cmd_topic},
	{"wc", cmd_wc},
	{}
};

static size_t prefix_len(const char *cmd)
{
	size_t ret = 0;

	while (*cmd && *cmd != ' ') {
		cmd++;
		ret++;
	}

	return ret;
}

static struct cmd *cmd_lookup(const char *cmd, const char **pars)
{
	struct cmd *c;
	size_t plen = prefix_len(cmd);

	*pars = cmd + plen + (cmd[plen] ? 1 : 0);

	for (c = cmds; c->cmd; c++) {
		if (!strncmp(c->cmd, cmd, plen))
			return c;
	}

	return NULL;
}

static void cmd_run(gp_widget *self, const char *cmd)
{
	const char *pars;
	struct cmd *c = cmd_lookup(++cmd, &pars);

	if (!c)
		gp_widget_log_append(self, "Invalid command");
	else
		c->cmd_run(self, pars);

	return;
}

static void cmd_status_log(gp_widget *self, const char *cmd)
{
	if (cmd[0] == '/') {
		cmd_run(self, cmd);
		return;
	}

	//???
}

static void cmd_channel(gp_widget *self, const char *cmd)
{
	if (cmd[0] == '/') {
		cmd_run(self, cmd);
		return;
	}

	struct channel *channel = self->priv;
	struct irc_conf *irc_conf = irc_get_ctx(irc_session);

	irc_cmd_msg(irc_session, channel->name, cmd);

	channels_printf(channel->name, "<%s> %s", irc_conf->nick, cmd);
}

int cmdline(gp_widget_event *ev)
{
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;

	if (ev->sub_type != GP_WIDGET_TBOX_TRIGGER)
		return 0;

	gp_widget *active = channels_active();
	const char *cmd = gp_widget_tbox_text(ev->self);

	if (channels_is_status_log(active))
		cmd_status_log(active, cmd);
	else
		cmd_channel(active, cmd);

	gp_widget_tbox_clear(ev->self);

	return 1;
}

static irc_callbacks_t callbacks = {
	.event_connect = event_connect,
	.event_join = event_join,
	.event_part = event_part,
	.event_nick = event_nick,
	.event_channel = event_channel,
	.event_topic = event_topic,
	.event_numeric = event_numeric,
};

static char *get_user_name(void)
{
	struct passwd *pw;
	char *ret;

	pw = getpwuid(getuid());

	ret = strdup(pw->pw_name);

	if (!ret)
		return "unknown";

	return ret;
}

int main(int argc, char *argv[])
{
	gp_htable *uids;
	gp_widget *layout = gp_app_layout_load("gpirc", &uids);

	if (!layout)
		return 1;

	irc_conf.nick = get_user_name();

	status_log = gp_widget_by_uid(uids, "status_log", GP_WIDGET_LOG);
	channel_tabs = gp_widget_by_uid(uids, "channel_tabs", GP_WIDGET_TABS);
	topic = gp_widget_by_uid(uids, "topic", GP_WIDGET_LABEL);

	if (channel_tabs)
		gp_widget_on_event_set(channel_tabs, channels_on_event, NULL);

	gp_htable_free(uids);

	if (channels_init())
		return 1;

	irc_session = irc_create_session(&callbacks);
	if (!irc_session)
		return 1;

	irc_set_ctx(irc_session, &irc_conf);
	do_connect();
	gp_widgets_main_loop(layout, "gpirc", NULL, argc, argv);

	return 0;
}
