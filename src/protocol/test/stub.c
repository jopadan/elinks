#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elinks.h"

#include "bfu/msgbox.h"
#include "main/module.h"
#include "protocol/file/mailcap.h"
#include "protocol/user.h"
#include "session/session.h"
#include "util/test.h"

enum retval {
	RET_OK,		/* All is well */
	RET_ERROR,	/* Failed to fetch URL or write document when dumping */
	RET_SIGNAL,	/* Catched SIGTERM which forced program to stop */
	RET_SYNTAX,	/* Cmdline syntax error or bad or missing dump URL */
	RET_FATAL,	/* Fatal error occurred during initialization */
	RET_PING,	/* --remote "ping()" found no running ELinkses */
	RET_REMOTE,	/* --remote failed to connect to a running ELinks */
	RET_COMMAND,	/* Used internally for exiting from cmdline commands */
};


struct program {
	int terminate;
	enum retval retval;
	char *path;
} program;

#define STUB_MODULE(name)				\
	struct module name = struct_module(		\
		/* name: */		"Stub " #name,	\
		/* options: */		NULL,		\
		/* hooks: */		NULL,		\
		/* submodules: */	NULL,		\
		/* data: */		NULL,		\
		/* init: */		NULL,		\
		/* done: */		NULL,		\
		/* getname: */	NULL	\
	)
STUB_MODULE(auth_module);
STUB_MODULE(bittorrent_protocol_module);
STUB_MODULE(cgi_protocol_module);
STUB_MODULE(dgi_protocol_module);
STUB_MODULE(file_protocol_module);
STUB_MODULE(finger_protocol_module);
STUB_MODULE(fsp_protocol_module);
STUB_MODULE(ftp_protocol_module);
STUB_MODULE(ftpes_protocol_module);
STUB_MODULE(gemini_protocol_module);
STUB_MODULE(gopher_protocol_module);
STUB_MODULE(http_protocol_module);
STUB_MODULE(nntp_protocol_module);
STUB_MODULE(sftp_protocol_module);
STUB_MODULE(smb_protocol_module);
STUB_MODULE(spartan_protocol_module);
STUB_MODULE(uri_rewrite_module);
STUB_MODULE(user_protocol_module);

static void
stub_called(const char *fun)
{
	ELOG
	die("FAIL: stub %s\n", fun);
}

#define STUB_PROTOCOL_HANDLER(name)		\
	void					\
	name(struct connection *conn)		\
	{					\
		stub_called(#name);		\
	}					\
	protocol_handler_T name /* consume semicolon */
#define STUB_PROTOCOL_EXTERNAL_HANDLER(name)		\
	void						\
	name(struct session *ses, struct uri *uri)	\
	{						\
		stub_called(#name);			\
	}						\
	protocol_external_handler_T name /* consume semicolon */
STUB_PROTOCOL_HANDLER(about_protocol_handler);
STUB_PROTOCOL_HANDLER(bittorrent_protocol_handler);
STUB_PROTOCOL_HANDLER(bittorrent_peer_protocol_handler);
STUB_PROTOCOL_HANDLER(data_protocol_handler);
STUB_PROTOCOL_HANDLER(dgi_protocol_handler);
STUB_PROTOCOL_EXTERNAL_HANDLER(ecmascript_protocol_handler);
STUB_PROTOCOL_HANDLER(file_protocol_handler);
STUB_PROTOCOL_HANDLER(finger_protocol_handler);
STUB_PROTOCOL_HANDLER(fsp_protocol_handler);
STUB_PROTOCOL_HANDLER(ftp_protocol_handler);
STUB_PROTOCOL_HANDLER(ftpes_protocol_handler);
STUB_PROTOCOL_HANDLER(gemini_protocol_handler);
STUB_PROTOCOL_HANDLER(gopher_protocol_handler);
STUB_PROTOCOL_HANDLER(http_protocol_handler);
STUB_PROTOCOL_HANDLER(news_protocol_handler);
STUB_PROTOCOL_HANDLER(nntp_protocol_handler);
STUB_PROTOCOL_HANDLER(proxy_protocol_handler);
STUB_PROTOCOL_HANDLER(sftp_protocol_handler);
STUB_PROTOCOL_HANDLER(smb_protocol_handler);
STUB_PROTOCOL_HANDLER(spartan_protocol_handler);
STUB_PROTOCOL_EXTERNAL_HANDLER(user_protocol_handler);

/* declared in "protocol/user.h" */
char *
get_user_program(struct terminal *term, const char *progid, int progidlen)
{
	ELOG
	stub_called("get_user_program");
	return NULL;
}

/* declared in "session/session.h" */
void
print_error_dialog(struct session *ses, struct connection_state state,
		   struct uri *uri, connection_priority_T priority)
{
	ELOG
	stub_called("print_error_dialog");
}

/* declared in "bfu/msgbox.h" */
char *
msg_text(struct terminal *term, const char *format, ...)
{
	ELOG
	stub_called("msg_text");
	return NULL;
}

/* declared in "bfu/msgbox.h" */
struct dialog_data *
msg_box(struct terminal *term, struct memory_list *mem_list,
	msgbox_flags_T flags, char *title, format_align_T align,
	char *text, void *udata, int buttons, ...)
{
	ELOG
	/* mem_list should be freed here but because this is just a
	 * test program it won't matter.  */
	stub_called("msg_box");
	return NULL;
}

/* declared in "protocol/file/mailcap.h" */
void
mailcap_protocol_handler(struct connection *conn)
{
	ELOG
	stub_called("mailcap_protocol_handler");
}
