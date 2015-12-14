/* $OpenBSD: cmd-lock-server.c,v 1.21 2015/12/14 00:31:54 nicm Exp $ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include "tmux.h"

/*
 * Lock commands.
 */

enum cmd_retval	 cmd_lock_server_exec(struct cmd *, struct cmd_q *);

const struct cmd_entry cmd_lock_server_entry = {
	.name = "lock-server",
	.alias = "lock",

	.args = { "", 0, 0 },
	.usage = "",

	.flags = 0,
	.exec = cmd_lock_server_exec
};

const struct cmd_entry cmd_lock_session_entry = {
	.name = "lock-session",
	.alias = "locks",

	.args = { "t:", 0, 0 },
	.usage = CMD_TARGET_SESSION_USAGE,

	.tflag = CMD_SESSION,

	.flags = 0,
	.exec = cmd_lock_server_exec
};

const struct cmd_entry cmd_lock_client_entry = {
	.name = "lock-client",
	.alias = "lockc",

	.args = { "t:", 0, 0 },
	.usage = CMD_TARGET_CLIENT_USAGE,

	.tflag = CMD_CLIENT,

	.flags = 0,
	.exec = cmd_lock_server_exec
};

enum cmd_retval
cmd_lock_server_exec(struct cmd *self, __unused struct cmd_q *cmdq)
{
	if (self->entry == &cmd_lock_server_entry)
		server_lock();
	else if (self->entry == &cmd_lock_session_entry)
		server_lock_session(cmdq->state.tflag.s);
	else
		server_lock_client(cmdq->state.c);

	recalculate_sizes();

	return (CMD_RETURN_NORMAL);
}
