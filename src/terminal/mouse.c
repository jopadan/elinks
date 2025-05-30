/** Support for mouse interface
 * @file */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* TODO: cleanup includes */

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef __hpux__
#include <limits.h>
#define HPUX_PIPE	(len > PIPE_BUF || errno != EAGAIN)
#else
#define HPUX_PIPE	1
#endif

#include "elinks.h"

#include "config/home.h"
#include "config/options.h"
#include "dialogs/status.h"
#include "intl/libintl.h"
#include "main/select.h"
#include "main/timer.h"
#include "osdep/ascii.h"
#include "osdep/osdep.h"
#include "session/session.h"
#include "terminal/hardio.h"
#include "terminal/itrm.h"
#include "terminal/kbd.h"
#include "terminal/mouse.h"
#include "terminal/terminal.h"
#include "util/error.h"
#include "util/memory.h"
#include "util/string.h"
#include "util/time.h"

extern struct itrm *ditrm;

#define write_sequence(fd, seq) \
	hard_write(fd, seq, sizeof(seq) - 1)


#define INIT_TWIN_MOUSE_SEQ	"\033[?9h"	/**< Send MIT Mouse Row & Column on Button Press */
#define INIT_XWIN_MOUSE_SEQ	"\033[?1000h\033[?1002h\033[?1005l\033[?1015l\033[?1006h"	/**< Send Mouse X & Y on button press and release */

void
send_mouse_init_sequence(int h)
{
	ELOG
	write_sequence(h, INIT_TWIN_MOUSE_SEQ);
	write_sequence(h, INIT_XWIN_MOUSE_SEQ);
}

#define DONE_TWIN_MOUSE_SEQ	"\033[?9l"	/**< Don't Send MIT Mouse Row & Column on Button Press */
#define DONE_XWIN_MOUSE_SEQ	"\033[?1000l\033[?1002l\033[?1006l"	/**< Don't Send Mouse X & Y on button press and release */

void
send_mouse_done_sequence(int h)
{
	ELOG
	/* This is a hack to make xterm + alternate screen working,
	 * if we send only DONE_XWIN_MOUSE_SEQ, mouse is not totally
	 * released it seems, in rxvt and xterm... --Zas */
	write_sequence(h, DONE_TWIN_MOUSE_SEQ);
	write_sequence(h, DONE_XWIN_MOUSE_SEQ);
}

int mouse_enabled;

void
disable_mouse(void)
{
	ELOG
	if (!mouse_enabled) return;

	unhandle_mouse(ditrm->mouse_h);
	if (is_xterm()) send_mouse_done_sequence(get_output_handle());

	mouse_enabled = 0;
}

static int
mouse_lock_exists(void)
{
	ELOG
	char *lock_filename = straconcat(empty_string_or_(get_xdg_config_home()), "mouse.lock", (char *) NULL);
	int res = 0;

	if (lock_filename) {
		res = !access(lock_filename, F_OK);
		mem_free(lock_filename);
	}

	return res;
}

void
enable_mouse(void)
{
	ELOG
	if (get_opt_bool("ui.mouse_disable", NULL))
		return;

	if (mouse_lock_exists()) {
		return;
	}

	if (mouse_enabled) return;

	if (is_xterm()) send_mouse_init_sequence(get_output_handle());
	ditrm->mouse_h = handle_mouse(0, (void (*)(void *, char *, int)) itrm_queue_event, ditrm);

	mouse_enabled = 1;
}

void
toggle_mouse(struct session *ses)
{
	ELOG
	if (mouse_enabled) {
		disable_mouse();
	} else {
		enable_mouse();
	}

	if (mouse_enabled) {
		mem_free_set(&ses->status.window_status, stracpy(_("Mouse enabled", ses->tab->term)));
	} else {
		mem_free_set(&ses->status.window_status, stracpy(_("Mouse disabled", ses->tab->term)));
	}
	print_screen_status(ses);
}

static int
decode_mouse_position(struct itrm *itrm, int from)
{
	ELOG
	int position;

	position = (unsigned char) (itrm->in.queue.data[from]) - ' ' - 1
		 + ((int) ((unsigned char) (itrm->in.queue.data[from + 1]) - ' ' - 1) << 7);

	return (position & (1 << 13)) ? 0 : position;
}

#define get_mouse_x_position(itrm, esclen) decode_mouse_position(itrm, (esclen) + 1)
#define get_mouse_y_position(itrm, esclen) decode_mouse_position(itrm, (esclen) + 3)

#define TW_BUTT_LEFT	1
#define TW_BUTT_MIDDLE	2
#define TW_BUTT_RIGHT	4

static int xterm_button = -1;

/** @returns length of the escape sequence or -1 if the caller needs to set up
 * the ESC delay timer. */
int
decode_terminal_mouse_escape_sequence(struct itrm *itrm, struct interlink_event *ev,
				      int el, int v)
{
	ELOG
	struct interlink_event_mouse mouse;

	if (itrm->in.queue.len - el < 3)
		return -1;

	if (v == 5) {
		if (xterm_button == -1)
			xterm_button = 0;

		if (itrm->in.queue.len - el < 5)
			return -1;

		mouse.x = get_mouse_x_position(itrm, el);
		mouse.y = get_mouse_y_position(itrm, el);

		switch ((itrm->in.queue.data[el] - ' ') ^ xterm_button) { /* Every event changes only one bit */
		case TW_BUTT_LEFT:
			mouse.button = B_LEFT | ((xterm_button & TW_BUTT_LEFT) ? B_UP : B_DOWN);
			break;
		case TW_BUTT_MIDDLE:
			mouse.button = B_MIDDLE | ((xterm_button & TW_BUTT_MIDDLE) ? B_UP : B_DOWN);
			break;
		case TW_BUTT_RIGHT:
			mouse.button = B_RIGHT | ((xterm_button & TW_BUTT_RIGHT) ? B_UP : B_DOWN);
			break;
		case 0:
			mouse.button = B_DRAG;
			break;
		default:
			mouse.button = 0; /* shut up warning */
		/* default : Twin protocol error */
		}

		xterm_button = itrm->in.queue.data[el] - ' ';
		el += 5;

	} else {
		/* See terminal/mouse.h about details of the mouse reporting
		 * protocol and {struct term_event_mouse->button} bitmask
		 * structure. */
		mouse.x = itrm->in.queue.data[el+1] - ' ' - 1;
		mouse.y = itrm->in.queue.data[el+2] - ' ' - 1;

		/* There are rumours arising from remnants of code dating to
		 * the ancient Mikulas' times that bit 4 indicated B_DRAG.
		 * However, I didn't find on what terminal it should be ever
		 * supposed to work and it conflicts with wheels. So I removed
		 * the last remnants of the code as well. --pasky */

		mouse.button = (itrm->in.queue.data[el] & 7) | B_DOWN;
		/* smartglasses1 - rxvt wheel: */
		if (mouse.button == 3 && xterm_button != -1) {
			mouse.button = xterm_button | B_UP;
		}
		/* xterm wheel: */
		if ((itrm->in.queue.data[el] & 96) == 96) {
			mouse.button = (itrm->in.queue.data[el] & 1) ? B_WHEEL_DOWN : B_WHEEL_UP;
		}

		xterm_button = -1;
		/* XXX: Eterm/aterm uses rxvt-like reporting, but sends the
		 * release sequence for wheel. rxvt itself sends only press
		 * sequence. Since we can't reliably guess what we're talking
		 * with from $TERM, we will rather support Eterm/aterm, as in
		 * rxvt, at least each second wheel up move will work. */
		if (mouse_action_is(&mouse, B_DOWN))
			xterm_button = mouse_get_button(&mouse);

		el += 3;
	}

	/* Postpone changing of the event type until all sanity
	 * checks have been done. */
	set_mouse_interlink_event(ev, mouse.x, mouse.y, mouse.button);

	return el;
}

int
decode_terminal_mouse_escape_sequence_256(struct itrm *itrm, struct interlink_event *ev,
				      int el, int v)
{
	ELOG
	struct interlink_event_mouse mouse;
	/* SGR 1006 mouse extension: \e[<b;x;yM where b, x and y are in decimal, no longer offset by 32,
	   and the trailing letter is 'm' instead of 'M' for mouse release so that the released button is reported. */
	int eel;
	int x = 0, y = 0, b = 0;
	unsigned char ch = 0;
	eel = el;

	while (1) {
		if (el == itrm->in.queue.len) return -1;
		if (el - eel >= 9) return el;
		ch = itrm->in.queue.data[el++];
		if (ch == ';') break;
		if (ch < '0' || ch > '9') return el;
		b = 10 * b + (ch - '0');
	}
	eel = el;

	while (1) {
		if (el == itrm->in.queue.len) return -1;
		if (el - eel >= 9) return el;
		ch = itrm->in.queue.data[el++];
		if (ch == ';') break;
		if (ch < '0' || ch > '9') return el;
		x = 10 * x + (ch - '0');
	}
	eel = el;

	while (1) {
		if (el == itrm->in.queue.len) return -1;
		if (el - eel >= 9) return el;
		ch = itrm->in.queue.data[el++];
		if (ch == 'M' || ch == 'm') break;
		if (ch < '0' || ch > '9') return el;
		y = 10 * y + (ch - '0');
	}

	mouse.x = x - 1;
	mouse.y = y - 1;

	if (ch == 'm') mouse.button = B_UP;
	else if ((b & 0x20) == 0x20) mouse.button = B_DRAG, b &= ~0x20;
	else mouse.button = B_DOWN;

	if (b == 0) mouse.button |= B_LEFT;
	else if (b == 1) mouse.button |= B_MIDDLE;
	else if (b == 2) mouse.button |= B_RIGHT;
	else if (b == 3 && xterm_button >= 0) mouse.button |= xterm_button;
	else if (b == 0x40) mouse.button |= B_WHEEL_UP;
	else if (b == 0x41) mouse.button |= B_WHEEL_DOWN;


	xterm_button = -1;
	if (mouse_action_is(&mouse, B_DOWN)) {
		xterm_button = mouse_get_button(&mouse);
	}

	/* Postpone changing of the event type until all sanity
	 * checks have been done. */
	set_mouse_interlink_event(ev, mouse.x, mouse.y, mouse.button);

	return el;
}
