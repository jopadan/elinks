/* These cute LightEmittingDiode-like indicators. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <time.h>

#include "elinks.h"

#include "bfu/leds.h"
#include "config/options.h"
#include "document/document.h"
#include "document/view.h"
#include "intl/libintl.h"
#include "main/module.h"
#include "main/timer.h"
#include "osdep/osdep.h"
#include "session/session.h"
#include "terminal/draw.h"
#include "terminal/tab.h"
#include "terminal/terminal.h"
#include "terminal/window.h"
#include "util/color.h"
#include "util/error.h"
#include "util/time.h"
#include "viewer/timer.h"

/* Current leds allocation:
 * 0 - SSL connection indicator
 * 1 - Insert-mode indicator
 * 2 - JavaScript Error indicator
 * 3 - JavaScript pop-up blocking indicator
 * 4 - unused, reserved for Lua
 * 5 - download in progress */

/* XXX: Currently, the leds toggling is quite hackish, some more work should go
 * to it (ie. some led hooks called in sync_leds() to light the leds
 * dynamically. --pasky */

/* Always reset led to '-' when not used anymore. */

/* If we would do real protection, we would do this as array of pointers. This
 * way someone can just get any struct led and add/subscribe appropriate struct
 * led for his control; however, I bet on programmers' responsibility rather,
 * and hope that everyone will abide the "rules". */

static int timer_duration_backup = 0;

static timer_id_T redraw_timer = TIMER_ID_UNDEF;
static int drawing = 0;

static void redraw_leds(void *);

enum led_option {
	LEDS_CLOCK_TREE,
	LEDS_CLOCK_ENABLE,
	LEDS_CLOCK_FORMAT,
	LEDS_CLOCK_ALIAS,

	LEDS_SHOW_IP_ENABLE,
	LEDS_SHOW_MEM_ENABLE,

	LEDS_TEMPERATURE_TREE,
	LEDS_TEMPERATURE_ENABLE,
	LEDS_TEMPERATURE_FILENAME,

	LEDS_PANEL_TREE,
	LEDS_PANEL_ENABLE,

	LEDS_OPTIONS,
};

static union option_info led_options[] = {
	INIT_OPT_TREE("ui", N_("Clock"),
		"clock", OPT_ZERO, N_("Digital clock in the status bar.")),

	INIT_OPT_BOOL("ui.clock", N_("Enable"),
		"enable", OPT_ZERO, 0,
		N_("Whether to display a digital clock in the status bar.")),

	INIT_OPT_STRING("ui.clock", N_("Format"),
		"format", OPT_ZERO, "[%H:%M]",
		N_("Format string for the digital clock. See the strftime(3) "
		"manpage for details.")),


	/* Compatibility alias. Added: 2004-04-22, 0.9.CVS. */
	INIT_OPT_ALIAS("ui.timer", "clock", OPT_ZERO, "ui.clock"),

	INIT_OPT_BOOL("ui", N_("Show IP"),
		"show_ip", OPT_ZERO, 0,
		N_("Whether to display IP of the document in the status bar.")),

	INIT_OPT_BOOL("ui", N_("Show available memory"),
		"show_mem", OPT_ZERO, 0,
		N_("Whether to display available memory. From /proc/meminfo.")),

	INIT_OPT_TREE("ui", N_("Temperature"),
		"temperature", OPT_ZERO, N_("Temperature of CPU.")),

	INIT_OPT_BOOL("ui.temperature", N_("Enable"),
		"enable", OPT_ZERO, 0,
		N_("Whether to display temperature of the CPU in the status bar.")),

	INIT_OPT_STRING("ui.temperature", N_("Filename"),
		"filename", OPT_ZERO, "/sys/class/thermal/thermal_zone0/temp",
		N_("Filename to see temperature.")),


	INIT_OPT_TREE("ui", N_("LEDs"),
		"leds", OPT_ZERO,
		N_("LEDs (visual indicators) options.")),

	INIT_OPT_BOOL("ui.leds", N_("Enable"),
		"enable", OPT_ZERO, 1,
		N_("Enable LEDs. These visual indicators will inform you "
		"about various states.")),

	NULL_OPTION_INFO,
};

#define get_opt_leds(which)		led_options[(which)].option.value
#define get_leds_clock_enable()		get_opt_leds(LEDS_CLOCK_ENABLE).number
#define get_leds_clock_format()		get_opt_leds(LEDS_CLOCK_FORMAT).string
#define get_leds_panel_enable()		get_opt_leds(LEDS_PANEL_ENABLE).number
#define get_leds_show_ip_enable()	get_opt_leds(LEDS_SHOW_IP_ENABLE).number
#define get_leds_show_mem_enable()	get_opt_leds(LEDS_SHOW_MEM_ENABLE).number
#define get_leds_temperature_enable()	get_opt_leds(LEDS_TEMPERATURE_ENABLE).number
#define get_leds_temperature_filename()		get_opt_leds(LEDS_TEMPERATURE_FILENAME).string

void
init_leds(struct module *module)
{
	ELOG
	timer_duration_backup = 0;

	/* We can't setup timer here, because we may not manage to startup in
	 * 100ms and we will get to problems when we will call draw_leds() on
	 * uninitialized terminal. So, we will wait for draw_leds(). */
}

void
done_leds(struct module *module)
{
	ELOG
	kill_timer(&redraw_timer);
}

void
set_led_value(struct led *led, unsigned char value)
{
	ELOG
	if (value != led->value__) {
		led->value__ = value;
		led->value_changed__ = 1;
	}
}

unsigned char
get_led_value(struct led *led)
{
	ELOG
	return led->value__;
}

void
unset_led_value(struct led *led)
{
	ELOG
	set_led_value(led, '-');
}


void
init_led_panel(struct led_panel *leds)
{
	ELOG
	int i;

	for (i = 0; i < LEDS_COUNT; i++) {
		leds->leds[i].used__ = 0;
		unset_led_value(&leds->leds[i]);
	}
}

static int
draw_timer(struct terminal *term, int xpos, int ypos, struct color_pair *color)
{
	ELOG
	char s[64];
	int i, length;

	snprintf(s, sizeof(s), "[%d]", get_timer_duration());
	length = strlen(s);

	for (i = length - 1; i >= 0; i--)
		draw_char(term, xpos - (length - i), ypos, s[i], 0, color);

	return length;
}

static int
draw_show_ip(struct session *ses, int xpos, int ypos, struct color_pair *color)
{
	ELOG
	if (ses->doc_view && ses->doc_view->document && ses->doc_view->document->ip) {
		struct terminal *term = ses->tab->term;
		char *s = ses->doc_view->document->ip;
		int length = strlen(s);
		int i;

		for (i = length - 1; i >= 0; i--)
			draw_char(term, xpos - (length - i), ypos, s[i], 0, color);

		return length;
	}
	return 0;
}

static int
draw_show_mem(struct session *ses, int xpos, int ypos, struct color_pair *color)
{
	ELOG
	struct terminal *term = ses->tab->term;
	struct string text;
	int i;
	int length;
	char *pos;

	if (!init_string(&text)) {
		return 0;
	}
	add_format_to_string(&text, "[%ld MiB]", os_get_free_mem_in_mib());
	length = text.length;
	for (i = 0, pos = text.source; i < length; i++) {
		draw_char(term, xpos - length + i, ypos, pos[i], 0, color);
	}
	done_string(&text);

	return length;
}

static int
draw_temperature(struct session *ses, int xpos, int ypos, struct color_pair *color)
{
	ELOG
	struct terminal *term = ses->tab->term;
	int temp = 0;
	int ret;
	struct string text;
	int i;
	int length;
	char *pos;
	FILE *f = fopen(get_leds_temperature_filename(), "r");

	if (!f) {
		return 0;
	}
	ret = fscanf(f, "%d", &temp);
	fclose(f);

	if (ret < 1) {
		return 0;
	}

	if (!init_string(&text)) {
		return 0;
	}
	add_format_to_string(&text, "[%d°C]", (int)(temp * 0.001 + 0.5));
#ifdef CONFIG_UTF8
	length = utf8_ptr2cells(text.source, NULL);
	char *end = text.source + text.length;
#else
	length = text.length;
#endif
	for (i = 0, pos = text.source; i < length; i++) {
#ifdef CONFIG_UTF8
		unicode_val_T data = utf8_to_unicode(&pos, end);
		if (data == UCS_NO_CHAR) {
			--i;
			continue;
		}
#else
		unsigned char data = pos[i];
#endif
		draw_char(term, xpos - length + i, ypos, data, 0, color);
	}
	done_string(&text);

	return length;
}

#ifdef HAVE_STRFTIME
static int
draw_clock(struct terminal *term, int xpos, int ypos, struct color_pair *color)
{
	ELOG
	char s[64];
	time_t curtime = time(NULL);
	struct tm *loctime = localtime(&curtime);
	int i, length;

	length = strftime(s, sizeof(s), get_leds_clock_format(), loctime);
	s[length] = '\0';
	for (i = length - 1; i >= 0; i--)
		draw_char(term, xpos - (length - i), ypos, s[i], 0, color);

	return length;
}
#endif

static milliseconds_T
compute_redraw_interval(void)
{
	ELOG
	if (are_there_downloads())
		return 100;

	/* TODO: Check whether the time format includes seconds.  If not,
	 * return milliseconds to next minute. */

	if (get_leds_clock_enable())
		return 1000;

	return 0;
}

void
draw_leds(struct session *ses)
{
	ELOG
	struct terminal *term = ses->tab->term;
	struct color_pair *led_color = NULL;
	int i;
	int xpos = term->width - LEDS_COUNT - 3;
	int ypos = term->height - 1;

	term->leds_length = 0;

	/* This should be done elsewhere, but this is very nice place where we
	 * could do that easily. */
	if (get_opt_int("ui.timer.enable", NULL) == 2) {
		led_color = get_bfu_color(term, "status.status-text");
		if (!led_color) goto end;

		term->leds_length += draw_timer(term, xpos, ypos, led_color);
	}

	if (!get_leds_panel_enable()) return;

	if (!led_color) {
		led_color = get_bfu_color(term, "status.status-text");
		if (!led_color) goto end;
	}

#ifdef HAVE_STRFTIME
	if (get_leds_clock_enable()) {
		term->leds_length += draw_clock(term, xpos - term->leds_length, ypos, led_color);
	}
#endif

	if (get_leds_temperature_enable()) {
		struct color_pair *color = get_bfu_color(term, "status.status-text");

		if (color) term->leds_length += draw_temperature(ses, xpos - term->leds_length, ypos, color);
	}

	if (get_leds_show_mem_enable()) {
		struct color_pair *color = get_bfu_color(term, "status.showmem-text");

		if (color) term->leds_length += draw_show_mem(ses, xpos - term->leds_length, ypos, color);
	}

	if (get_leds_show_ip_enable()) {
		struct color_pair *color = get_bfu_color(term, "status.showip-text");

		if (color) term->leds_length += draw_show_ip(ses, xpos - term->leds_length, ypos, color);
	}

	/* We must shift the whole thing by one char to left, because we don't
	 * draft the char in the right-down corner :(. */

	draw_char(term, xpos, ypos, '[', 0, led_color);

	for (i = 0; i < LEDS_COUNT; i++) {
		struct led *led = &ses->status.leds.leds[i];

		draw_char(term, xpos + i + 1, ypos, led->value__, 0, led_color);
		led->value_changed__ = 0;
	}

	draw_char(term, xpos + LEDS_COUNT + 1, ypos, ']', 0, led_color);

	term->leds_length += LEDS_COUNT + 2;

end:
#ifdef CONFIG_UTF8
	if (term->utf8_cp) {
		struct el_box box;
		set_box(&box, xpos, ypos, LEDS_COUNT + 1, 1);
		fix_dwchar_around_box(term, &box, 0, 0, 0);
	}
#endif
	/* Redraw each 100ms. */
	if (!drawing && redraw_timer == TIMER_ID_UNDEF) {
		milliseconds_T delay = compute_redraw_interval();

		if (delay)
			install_timer(&redraw_timer, delay, redraw_leds, NULL);
	}
}

/* Determine if leds redrawing is necessary. Returns non-zero if so. */
static int
sync_leds(struct session *ses)
{
	ELOG
	int i;
	int timer_duration;

#ifdef HAVE_STRFTIME
	/* Check if clock was enabled and update if needed. */
	if (get_leds_clock_enable()) {
		/* We _always_ update when clock is enabled
		 * Not perfect. --Zas */
		return 1;
	}
#endif

	for (i = 0; i < LEDS_COUNT; i++) {
		struct led *led = &ses->status.leds.leds[i];

		if (led->value_changed__)
			return 1;
	}

	/* Check if timer was updated. */
	timer_duration = get_timer_duration();
	if (timer_duration_backup != timer_duration) {
		timer_duration_backup = timer_duration;
		return 1;
	}

	return 0;
}

static void
update_download_led(struct session *ses)
{
	ELOG
	struct session_status *status = &ses->status;

	if (are_there_downloads()) {
		unsigned char led = get_led_value(status->download_led);

		switch (led) {
			case '-' : led = '\\'; break;
			case '\\': led = '|'; break;
			case '|' : led = '/'; break;
			default: led = '-';
		}

		set_led_value(status->download_led, led);
	} else {
		unset_led_value(status->download_led);
	}
}

/* Timer callback for @redraw_timer.  As explained in @install_timer,
 * this function must erase the expired timer ID from all variables.  */
static void
redraw_leds(void *xxx)
{
	ELOG
	struct terminal *term;
	milliseconds_T delay;

	redraw_timer = TIMER_ID_UNDEF;

	if (!get_leds_panel_enable()
	    && get_opt_int("ui.timer.enable", NULL) != 2) {
		return;
	}

	delay = compute_redraw_interval();
	if (delay)
		install_timer(&redraw_timer, delay, redraw_leds, NULL);

	if (drawing) return;
	drawing = 1;

	foreach (term, terminals) {
		struct session *ses;
		struct window *win;
		
		if (list_empty(term->windows)) continue;
		
		win = get_current_tab(term);
		assert(win);
		ses = (struct session *)win->data;

		update_download_led(ses);
		if (!sync_leds(ses))
			continue;
		redraw_terminal(term);
		draw_leds(ses);
	}
	drawing = 0;
}

void
menu_leds_info(struct terminal *term, void *xxx, void *xxxx)
{
	ELOG
	/* If LEDs ever get more dynamic we might have to change this, but it
	 * should do for now. --jonas */
	info_box(term, MSGBOX_FREE_TEXT | MSGBOX_SCROLLABLE,
	 	 N_("LED indicators"), ALIGN_LEFT,
		 msg_text(term, N_("What the different LEDs indicate:\n"
		 	"\n"
			"[SIJP--]\n"
			" |||||`- Download in progress\n"
			" ||||`-- Unused\n"
			" |||`--- A JavaScript pop-up window was blocked\n"
			" ||`---- A JavaScript error has occurred\n"
			" |`----- The state of insert mode for text-input form-fields\n"
			" |       'i' means modeless, 'I' means insert mode is on\n"
			" `------ Whether an SSL connection was used\n"
			"\n"
			"'-' generally indicates that the LED is off.")));
}


struct led *
register_led(struct session *ses, int number)
{
	ELOG
	struct led *led;

	if (number >= LEDS_COUNT || number < 0)
		return NULL;

	led = &ses->status.leds.leds[number];
	if (led->used__)
		return NULL;

	led->used__ = 1;

	return led;
}

void
unregister_led(struct led *led)
{
	ELOG
	assertm(led->used__, "Attempted to unregister unused led!");
	led->used__ = 0;
	unset_led_value(led);
}

struct module leds_module = struct_module(
	/* name: */		N_("LED indicators"),
	/* options: */		led_options,
	/* events: */		NULL,
	/* submodules: */	NULL,
	/* data: */		NULL,
	/* init: */		init_leds,
	/* done: */		done_leds,
	/* getname: */	NULL
);
