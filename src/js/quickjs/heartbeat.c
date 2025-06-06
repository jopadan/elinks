/* The QuickJS ECMAScript backend heartbeat fuctionality. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>    /* setitimer(2) */
#include <signal.h>

#include "elinks.h"

#include "config/options.h"
#include "document/view.h"
#include "js/ecmascript.h"
#include "js/quickjs.h"
#include "js/quickjs/heartbeat.h"
#include "osdep/signals.h"
#include "session/session.h"
#include "util/lists.h"
#include "util/math.h"   /* int_upper_bound */
#include "util/memory.h"
#include "viewer/text/vs.h"



static INIT_LIST_OF(struct heartbeat, heartbeats);

#if !defined(CONFIG_OS_DOS) && !defined(CONFIG_OS_WIN32)
static struct itimerval heartbeat_timer = { { 1, 0 }, { 1, 0 } };
#endif

/* This callback is installed by JS_SetInterruptHandler.
 * Returning 1 terminates script execution immediately. */

int
js_heartbeat_callback(JSRuntime *rt, void *opaque)
{
	ELOG
	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)opaque;

	if (!interpreter || !interpreter->heartbeat || interpreter->heartbeat->ttl > 0) {
		return 0;
	}
	return 1;
}

/* Callback for SIGVTALRM.  Go through all heartbeats, decrease each
 * one's TTL, and call JS_RequestInterruptCallback if a heartbeat's TTL
 * goes to 0. */
void
check_heartbeats(void *data)
{
	ELOG
	struct heartbeat *hb;

	foreach (hb, heartbeats) {
		assert(hb->interpreter);
		--hb->ttl;

		if (hb->ttl <= 0) {
			if (hb->interpreter->vs
				&& hb->interpreter->vs->doc_view
				&& hb->interpreter->vs->doc_view->session
				&& hb->interpreter->vs->doc_view->session->tab
				&& hb->interpreter->vs->doc_view->session->tab->term) {
				struct session *ses = hb->interpreter->vs->doc_view->session;
				struct terminal *term = ses->tab->term;
				int max_exec_time = get_opt_int("ecmascript.max_exec_time", ses);

				ecmascript_timeout_dialog(term, max_exec_time);
			}
		}
	}
#if !defined(CONFIG_OS_DOS) && !defined(CONFIG_OS_WIN32)
	install_signal_handler(SIGVTALRM, check_heartbeats, NULL, 1);
#endif
}

/* Create a new heartbeat for the given interpreter. */
struct heartbeat *
add_heartbeat(struct ecmascript_interpreter *interpreter)
{
	ELOG
	struct session *ses;
	struct heartbeat *hb;

	assert(interpreter);

	if (interpreter->heartbeat) {
		interpreter->heartbeat->ref_count++;
		return interpreter->heartbeat;
	}

	if (!interpreter->vs || !interpreter->vs->doc_view)
		ses = NULL;
	else
		ses = interpreter->vs->doc_view->session;

	hb = (struct heartbeat *)mem_alloc(sizeof(struct heartbeat));

	if (!hb) return NULL;

	hb->ttl = get_opt_int("ecmascript.max_exec_time", ses);
	hb->interpreter = interpreter;
	hb->ref_count = 1;
	add_to_list(heartbeats, hb);

#if !defined(CONFIG_OS_DOS) && !defined(CONFIG_OS_WIN32)
	/* Update the heartbeat timer. */
	if (list_is_singleton(*hb)) {
		heartbeat_timer.it_value.tv_sec = 1;
		setitimer(ITIMER_VIRTUAL, &heartbeat_timer, NULL);
	}

	/* We install the handler every call to add_heartbeat instead of only on
	 * module initialisation because other code may set other handlers for
	 * the signal.  */
	install_signal_handler(SIGVTALRM, check_heartbeats, NULL, 1);
#endif
	return hb;
}

/* Destroy the given heartbeat. */
void
done_heartbeat(struct heartbeat *hb)
{
	ELOG
	if (!hb) return; /* add_heartbeat returned NULL */
	assert(hb->interpreter);

	if (--hb->ref_count > 0) {
		return;
	}

#if !defined(CONFIG_OS_DOS) && !defined(CONFIG_OS_WIN32)
	/* Stop the heartbeat timer if this heartbeat is the only one. */
	if (list_is_singleton(*hb)) {
		heartbeat_timer.it_value.tv_sec = 0;
		setitimer(ITIMER_VIRTUAL, &heartbeat_timer, NULL);
	}
#endif

	del_from_list(hb);
	hb->interpreter->heartbeat = NULL;
	mem_free(hb);
}
