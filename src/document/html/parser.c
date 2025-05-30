/* HTML parser */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* XXX: we _WANT_ strcasestr() ! */
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elinks.h"

#include "bfu/listmenu.h"
#include "bfu/menu.h"
#include "document/css/apply.h"
#include "document/css/css.h"
#include "document/css/stylesheet.h"
#include "document/html/frames.h"
#include "document/html/parse-meta-refresh.h"
#include "document/html/parser/link.h"
#include "document/html/parser/stack.h"
#include "document/html/parser/parse.h"
#include "document/html/parser.h"
#include "document/html/renderer.h"
#include "document/options.h"
#include "document/renderer.h"
#include "intl/charsets.h"
#include "protocol/date.h"
#include "protocol/header.h"
#include "protocol/uri.h"
#include "session/task.h"
#include "terminal/draw.h"
#include "util/align.h"
#include "util/box.h"
#include "util/color.h"
#include "util/conv.h"
#include "util/error.h"
#include "util/memdebug.h"
#include "util/memlist.h"
#include "util/memory.h"
#include "util/string.h"

/* Unsafe macros */
#include "document/html/internal.h"

#ifdef CONFIG_LIBCSS
#include <libcss/libcss.h>
#include "document/libdom/css.h"
#endif

/* TODO: This needs rewrite. Yes, no kidding. */

static int
extract_color(struct html_context *html_context, char *a,
	      const char *attribute, color_T *rgb)
{
	ELOG
	char *value;
	int retval;

	value = get_attr_val(a, attribute, html_context->doc_cp);
	if (!value) return -1;

	retval = decode_color(value, strlen(value), rgb);
	mem_free(value);

	return retval;
}

int
get_color(struct html_context *html_context, char *a,
	  const char *attribute, color_T *rgb)
{
	ELOG
	if (!use_document_fg_colors(html_context->options))
		return -1;

	return extract_color(html_context, a, attribute, rgb);
}

int
get_color2(struct html_context *html_context, char *value_value, color_T *rgb)
{
	ELOG
	if (!use_document_fg_colors(html_context->options))
		return -1;

	if (!value_value)
		return -1;

	return decode_color(value_value, strlen(value_value), rgb);
}


int
get_bgcolor(struct html_context *html_context, char *a, color_T *rgb)
{
	ELOG
	if (!use_document_bg_colors(html_context->options))
		return -1;

	return extract_color(html_context, a, "bgcolor", rgb);
}

char *
get_target(struct document_options *options, char *a)
{
	ELOG
	/* FIXME (bug 784): options->cp is the terminal charset;
	 * should use the document charset instead.  */
	char *v = get_attr_val(a, "target", options->cp);

	if (!v) return NULL;

	if (!*v || !c_strcasecmp(v, "_self")) {
		mem_free_set(&v, stracpy(options->framename));
	}

	return v;
}


void
ln_break(struct html_context *html_context, int n)
{
	ELOG
	if (!n || html_top->invisible) return;
	while (n > html_context->line_breax) {
		html_context->line_breax++;
		html_context->line_break_f(html_context);
	}
	html_context->position = 0;
	html_context->putsp = HTML_SPACE_SUPPRESS;
}

void
put_chrs(struct html_context *html_context, const char *start, int len)
{
	ELOG
	if (html_is_preformatted())
		html_context->putsp = HTML_SPACE_NORMAL;

	if (!len || html_top->invisible)
		return;

	switch (html_context->putsp) {
	case HTML_SPACE_NORMAL:
		break;

	case HTML_SPACE_ADD:
		html_context->put_chars_f(html_context, " ", 1);
		html_context->position++;
		html_context->putsp = HTML_SPACE_SUPPRESS;

		/* Fall thru. */

	case HTML_SPACE_SUPPRESS:
		html_context->putsp = HTML_SPACE_NORMAL;
		if (isspace((unsigned char)start[0])) {
			start++, len--;

			if (!len) {
				html_context->putsp = HTML_SPACE_SUPPRESS;
				return;
			}
		}

		break;
	}

	if (isspace((unsigned char)start[len - 1]) && !html_is_preformatted()) {
		html_context->putsp = HTML_SPACE_SUPPRESS;
	}
	html_context->was_br = 0;

	html_context->put_chars_f(html_context, start, len);

	html_context->position += len;
	html_context->line_breax = 0;
	if (html_context->was_li > 0)
		html_context->was_li--;
}

void
set_fragment_identifier(struct html_context *html_context,
                        char *attr_name, const char *attr)
{
	ELOG
	char *id_attr;

	id_attr = get_attr_val(attr_name, attr, html_context->doc_cp);

	if (id_attr) {
		html_context->special_f(html_context, SP_TAG, id_attr);
		mem_free(id_attr);
	}
}

void
add_fragment_identifier(struct html_context *html_context,
                        struct part *part, char *attr)
{
	ELOG
	struct part *saved_part = html_context->part;

	html_context->part = part;
	html_context->special_f(html_context, SP_TAG, attr);
	html_context->part = saved_part;
}

#ifdef CONFIG_CSS

#ifdef CONFIG_LIBCSS
void
import_css2_stylesheet(struct html_context *html_context, struct uri *base_uri, const char *unterminated_url, int len)
{
	ELOG
	char *url;
	char *import_url;
	struct uri *uri;

	assert(html_context);
	assert(base_uri);

	if (!html_context->options->css_enable
	    || !html_context->options->css_import)
		return;

	/* unterminated_url might not end with '\0', but join_urls
	 * requires that, so make a copy.  */
	url = memacpy(unterminated_url, len);
	if (!url) return;

	/* HTML <head> urls should already be fine but we can.t detect them. */
	import_url = join_urls(base_uri, url);
	mem_free(url);

	if (!import_url) return;

	uri = get_uri(import_url, URI_BASE);
	mem_free(import_url);

	if (!uri) return;

	/* Request the imported stylesheet as part of the document ... */
	html_context->special_f(html_context, SP_STYLESHEET, uri);

	/* ... and then attempt to import from the cache. */
	import_css2(html_context, uri);

	done_uri(uri);
}
#endif

void
import_css_stylesheet(struct css_stylesheet *css, struct uri *base_uri,
		      const char *unterminated_url, int len)
{
	ELOG
	struct html_context *html_context = (struct html_context *)css->import_data;
	char *url;
	char *import_url;
	struct uri *uri;

	assert(html_context);
	assert(base_uri);

	if (!html_context->options->css_enable
	    || !html_context->options->css_import)
		return;

	/* unterminated_url might not end with '\0', but join_urls
	 * requires that, so make a copy.  */
	url = memacpy(unterminated_url, len);
	if (!url) return;

	/* HTML <head> urls should already be fine but we can.t detect them. */
	import_url = join_urls(base_uri, url);
	mem_free(url);

	if (!import_url) return;

	uri = get_uri(import_url, URI_BASE);
	mem_free(import_url);

	if (!uri) return;

	/* Request the imported stylesheet as part of the document ... */
	html_context->special_f(html_context, SP_STYLESHEET, uri);

	/* ... and then attempt to import from the cache. */
	import_css(css, uri);

	done_uri(uri);
}
#endif

/* Extract the extra information that is available for elements which can
 * receive focus. Call this from each element which supports tabindex or
 * accesskey. */
/* Note that in ELinks, we support those attributes (I mean, we call this
 * function) while processing any focusable element (otherwise it'd have zero
 * tabindex, thus messing up navigation between links), thus we support these
 * attributes even near tags where we're not supposed to (like IFRAME, FRAME or
 * LINK). I think this doesn't make any harm ;). --pasky */
void
html_focusable(struct html_context *html_context, char *a)
{
	ELOG
	char *accesskey;
	int cp;
	int tabindex;

	elformat.accesskey = 0;
	elformat.tabindex = 0x80000000;

	if (!a) return;

	cp = html_context->doc_cp;

	accesskey = get_attr_val(a, "accesskey", cp);
	if (accesskey) {
		elformat.accesskey = accesskey_string_to_unicode(accesskey);
		mem_free(accesskey);
	}

	tabindex = get_num(a, "tabindex", cp);
	if (0 < tabindex && tabindex < 32767) {
		elformat.tabindex = (tabindex & 0x7fff) << 16;
	}

	mem_free_set(&elformat.onclick, get_attr_val(a, "onclick", cp));
	mem_free_set(&elformat.ondblclick, get_attr_val(a, "ondblclick", cp));
	mem_free_set(&elformat.onmouseover, get_attr_val(a, "onmouseover", cp));
	mem_free_set(&elformat.onhover, get_attr_val(a, "onhover", cp));
	mem_free_set(&elformat.onfocus, get_attr_val(a, "onfocus", cp));
	mem_free_set(&elformat.onmouseout, get_attr_val(a, "onmouseout", cp));
	mem_free_set(&elformat.onblur, get_attr_val(a, "onblur", cp));
	mem_free_set(&elformat.onkeydown, get_attr_val(a, "onkeydown", cp));
	mem_free_set(&elformat.onkeyup, get_attr_val(a, "onkeyup", cp));
	mem_free_set(&elformat.onkeypress, get_attr_val(a, "onkeypress", cp));
}

void
html_skip(struct html_context *html_context, char *a)
{
	ELOG
	html_top->invisible = 1;
	html_top->type = ELEMENT_DONT_KILL;
}

static void
check_head_for_refresh(struct html_context *html_context, char *head)
{
	ELOG
	char *refresh;
	char *url = NULL;
	char *joined_url = NULL;
	unsigned long seconds;

	refresh = parse_header(head, "Refresh", NULL);
	if (!refresh) return;

	if (html_parse_meta_refresh(refresh, &seconds, &url) == 0) {
		if (!url) {
			/* If the URL parameter is missing assume that the
			 * document being processed should be refreshed. */
			url = get_uri_string(html_context->base_href,
					     URI_ORIGINAL);
		}
	}

	if (url)
		joined_url = join_urls(html_context->base_href, url);

	if (joined_url) {
		if (seconds > HTTP_REFRESH_MAX_DELAY)
			seconds = HTTP_REFRESH_MAX_DELAY;

		html_focusable(html_context, NULL);

		if (get_opt_bool("document.browse.show_refresh_link", NULL)) {
			put_link_line("Refresh: ", url, joined_url,
				      html_context->options->framename, html_context);
		}
		html_context->special_f(html_context, SP_REFRESH, seconds, joined_url);
	}

	mem_free_if(joined_url);
	mem_free_if(url);
	mem_free(refresh);
}

static void
check_head_for_cache_control(struct html_context *html_context,
                             char *head)
{
	ELOG
	char *d;
	int no_cache = 0;
	time_t expires = 0;

	if (get_opt_bool("document.cache.ignore_cache_control", NULL))
		return;

	/* XXX: Code duplication with HTTP protocol backend. */
	/* I am not entirely sure in what order we should process these
	 * headers and if we should still process Cache-Control max-age
	 * if we already set max age to date mentioned in Expires.
	 * --jonas */
	if ((d = parse_header(head, "Pragma", NULL))) {
		if (strstr(d, "no-cache")) {
			no_cache = 1;
		}
		mem_free(d);
	}

	if (!no_cache && (d = parse_header(head, "Cache-Control", NULL))) {
		if (strstr(d, "no-cache") || strstr(d, "must-revalidate")) {
			no_cache = 1;

		} else  {
			char *pos = strstr(d, "max-age=");

			assert(!no_cache);

			if (pos) {
				/* Grab the number of seconds. */
				timeval_T max_age, seconds;

				timeval_from_seconds(&seconds, atol(pos + 8));
				timeval_now(&max_age);
				timeval_add_interval(&max_age, &seconds);

				expires = timeval_to_seconds(&max_age);
			}
		}

		mem_free(d);
	}

	if (!no_cache && (d = parse_header(head, "Expires", NULL))) {
		/* Convert date to seconds. */
		if (strstr(d, "now")) {
			timeval_T now;

			timeval_now(&now);
			expires = timeval_to_seconds(&now);
		} else {
			expires = parse_date(&d, NULL, 0, 1);
		}

		mem_free(d);
	}

	if (no_cache)
		html_context->special_f(html_context, SP_CACHE_CONTROL);
	else if (expires)
		html_context->special_f(html_context,
				       SP_CACHE_EXPIRES, expires);
}

void
process_head(struct html_context *html_context, char *head)
{
	ELOG
	check_head_for_refresh(html_context, head);

	check_head_for_cache_control(html_context, head);
}




static int
look_for_map(char **pos, char *eof, struct uri *uri,
             struct document_options *options)
{
	ELOG
	char *al, *attr, *name;
	int namelen;

	while (*pos < eof && **pos != '<') {
		(*pos)++;
	}

	if (*pos >= eof) return 0;

	if (*pos + 2 <= eof && ((*pos)[1] == '!' || (*pos)[1] == '?')) {
		*pos = skip_comment(*pos, eof);
		return 1;
	}

	if (parse_element(*pos, eof, &name, &namelen, &attr, pos)) {
		(*pos)++;
		return 1;
	}

	if (c_strlcasecmp(name, namelen, "MAP", 3)) return 1;

	if (uri && uri->fragment) {
		/* FIXME (bug 784): options->cp is the terminal charset;
		 * should use the document charset instead.  */
		al = get_attr_val(attr, "name", options->cp);
		if (!al) return 1;

		if (c_strlcasecmp(al, -1, uri->fragment, uri->fragmentlen)) {
			mem_free(al);
			return 1;
		}

		mem_free(al);
	}

	return 0;
}

static int
look_for_tag(char **pos, char *eof,
	     char *name, int namelen, char **label)
{
	ELOG
	char *pos2;
	struct string str;

	if (!init_string(&str)) {
		/* Is this the right way to bail out? --jonas */
		*pos = eof;
		return 0;
	}

	pos2 = *pos;
	while (pos2 < eof && *pos2 != '<') {
		pos2++;
	}

	if (pos2 >= eof) {
		done_string(&str);
		*pos = eof;
		return 0;
	}
	if (pos2 - *pos)
		add_bytes_to_string(&str, *pos, pos2 - *pos);
	*label = str.source;

	*pos = pos2;

	if (*pos + 2 <= eof && ((*pos)[1] == '!' || (*pos)[1] == '?')) {
		*pos = skip_comment(*pos, eof);
		return 1;
	}

	if (parse_element(*pos, eof, NULL, NULL, NULL, &pos2)) return 1;

	if (c_strlcasecmp(name, namelen, "A", 1)
	    && c_strlcasecmp(name, namelen, "/A", 2)
	    && c_strlcasecmp(name, namelen, "MAP", 3)
	    && c_strlcasecmp(name, namelen, "/MAP", 4)
	    && c_strlcasecmp(name, namelen, "AREA", 4)
	    && c_strlcasecmp(name, namelen, "/AREA", 5)) {
		*pos = pos2;
		return 1;
	}

	return 0;
}

/** @return -1 if EOF is hit without the closing tag; 0 if the closing
 * tag is found (in which case this also adds *@a menu to *@a ml); or
 * 1 if this should be called again.  */
static int
look_for_link(char **pos, char *eof, struct menu_item **menu,
	      struct memory_list **ml, struct uri *href_base,
	      char *target_base, struct conv_table *ct,
	      struct document_options *options)
{
	ELOG
	char *attr, *href, *name, *target;
	char *label = NULL; /* shut up warning */
	struct link_def *ld;
	struct menu_item *nm;
	int nmenu;
	int namelen;

	while (*pos < eof && **pos != '<') {
		(*pos)++;
	}

	if (*pos >= eof) return -1;

	if (*pos + 2 <= eof && ((*pos)[1] == '!' || (*pos)[1] == '?')) {
		*pos = skip_comment(*pos, eof);
		return 1;
	}

	if (parse_element(*pos, eof, &name, &namelen, &attr, pos)) {
		(*pos)++;
		return 1;
	}

	if (!c_strlcasecmp(name, namelen, "A", 1)) {
		while (look_for_tag(pos, eof, name, namelen, &label));

		if (*pos >= eof) return -1;

	} else if (!c_strlcasecmp(name, namelen, "AREA", 4)) {
		/* FIXME (bug 784): options->cp is the terminal charset;
		 * should use the document charset instead.  */
		char *alt = get_attr_val(attr, "alt", options->cp);

		if (alt) {
			/* CSM_NONE because get_attr_val() already
			 * decoded entities.  */
			label = convert_string(ct, alt, strlen(alt),
			                       options->cp, CSM_NONE,
			                       NULL, NULL, NULL);
			mem_free(alt);
		} else {
			label = NULL;
		}

	} else if (!c_strlcasecmp(name, namelen, "/MAP", 4)) {
		/* This is the only successful return from here! */
		add_to_ml(ml, (void *) *menu, (void *) NULL);
		return 0;

	} else {
		return 1;
	}

	target = get_target(options, attr);
	if (!target) target = stracpy(empty_string_or_(target_base));
	if (!target) {
		mem_free_if(label);
		return 1;
	}

	ld = (struct link_def *)mem_alloc(sizeof(*ld));
	if (!ld) {
		mem_free_if(label);
		mem_free(target);
		return 1;
	}

	/* FIXME (bug 784): options->cp is the terminal charset;
	 * should use the document charset instead.  */
	href = get_url_val(attr, "href", options->cp);
	if (!href) {
		mem_free_if(label);
		mem_free(target);
		mem_free(ld);
		return 1;
	}


	ld->link = join_urls(href_base, href);
	mem_free(href);
	if (!ld->link) {
		mem_free_if(label);
		mem_free(target);
		mem_free(ld);
		return 1;
	}


	ld->target = target;
	for (nmenu = 0; !mi_is_end_of_menu(&(*menu)[nmenu]); nmenu++) {
		struct link_def *ll = (struct link_def *)(*menu)[nmenu].data;

		if (!strcmp(ll->link, ld->link) &&
		    !strcmp(ll->target, ld->target)) {
			mem_free(ld->link);
			mem_free(ld->target);
			mem_free(ld);
			mem_free_if(label);
			return 1;
		}
	}

	if (label) {
		clr_spaces(label);

		if (!*label) {
			mem_free(label);
			label = NULL;
		}
	}

	if (!label) {
		label = stracpy(ld->link);
		if (!label) {
			mem_free(target);
			mem_free(ld->link);
			mem_free(ld);
			return 1;
		}
	}

	nm = (struct menu_item *)mem_realloc(*menu, (nmenu + 2) * sizeof(*nm));
	if (nm) {
		*menu = nm;
		memset(&nm[nmenu], 0, 2 * sizeof(*nm));
		nm[nmenu].text = label;
		nm[nmenu].func = map_selected;
		nm[nmenu].data = ld;
		nm[nmenu].flags = NO_INTL;
	}

	add_to_ml(ml, (void *) ld, (void *) ld->link, (void *) ld->target,
		  (void *) label, (void *) NULL);

	return 1;
}


int
get_image_map(char *head, char *pos, char *eof,
	      struct menu_item **menu, struct memory_list **ml, struct uri *uri,
	      struct document_options *options, char *target_base,
	      int to, int def, int hdef)
{
	ELOG
	struct conv_table *ct;
	struct string hd;
	int look_result;

	if (!init_string(&hd)) return -1;

	if (head) add_to_string(&hd, head);
	/* FIXME (bug 784): cp is the terminal charset;
	 * should use the document charset instead.  */
	scan_http_equiv(pos, eof, &hd, NULL, options->cp);
	ct = get_convert_table(hd.source, to, def, NULL, NULL, hdef);
	done_string(&hd);

	*menu = (struct menu_item *)mem_calloc(1, sizeof(**menu));
	if (!*menu) return -1;

	while (look_for_map(&pos, eof, uri, options));

	if (pos >= eof) {
		mem_free(*menu);
		return -1;
	}

	*ml = NULL;

	do {
		/* This call can modify both *ml and *menu.  */
		look_result = look_for_link(&pos, eof, menu, ml, uri,
					    target_base, ct, options);
	} while (look_result > 0);

	if (look_result < 0) {
		freeml(*ml);
		mem_free(*menu);
		return -1;
	}

	return 0;
}




void *
init_html_parser_state(struct html_context *html_context,
                       enum html_element_mortality_type type,
                       int align, int margin, int width)
{
	ELOG
	html_stack_dup(html_context, type);

	par_elformat.align = align;

	if (type <= ELEMENT_IMMORTAL) {
		par_elformat.leftmargin = margin;
		par_elformat.rightmargin = margin;
		par_elformat.width = width;
		par_elformat.list_level = 0;
		par_elformat.list_number = 0;
		par_elformat.dd_margin = 0;
		html_top->namelen = 0;
	}

	return html_top;
}



void
done_html_parser_state(struct html_context *html_context,
                       void *state)
{
	ELOG
	struct html_element *element = (struct html_element *)state;

	html_context->line_breax = 1;

	while (html_top != element) {
		pop_html_element(html_context);
#if 0
		/* I've preserved this bit to show an example of the Old Code
		 * of the Mikulas days (I _HOPE_ it's by Mikulas, at least ;-).
		 * I think this assert() can never fail, for one. --pasky */
		assertm(html_top && (void *) html_top != (void *) &html_stack,
			"html stack trashed");
		if_assert_failed break;
#endif
	}

	html_top->type = ELEMENT_KILLABLE;
	pop_html_element(html_context);

}

/* This function does not set html_context.doc_cp = document.cp,
 * because it does not know the document, and because the codepage has
 * not even been decided when it is called.
 *
 * @param[out] title
 *   The title of the document.  This is in the document charset,
 *   and entities have not been decoded.  */
struct html_context *
init_html_parser(struct uri *uri, struct document *document,
		 char *start, char *end,
		 struct string *head, struct string *title,
		 void (*put_chars)(struct html_context *, const char *, int),
		 void (*line_break)(struct html_context *),
		 void *(*special)(struct html_context *, html_special_type_T, ...))
{
	ELOG
	struct html_context *html_context;
	struct html_element *e;
	struct document_options *options = &document->options;

	assert(uri && options);
	if_assert_failed return NULL;

	html_context = (struct html_context *)mem_calloc(1, sizeof(*html_context));
	if (!html_context) return NULL;

	html_context->document = document;
#ifdef CONFIG_CSS
	html_context->css_styles.import = import_css_stylesheet;
	init_css_selector_set(&html_context->css_styles.selectors);
#endif

	init_list(html_context->stack);

	html_context->startf = start;
	html_context->put_chars_f = put_chars;
	html_context->line_break_f = line_break;
	html_context->special_f = special;

	html_context->base_href = get_uri_reference(uri);
	html_context->base_target = null_or_stracpy(options->framename);

	html_context->options = options;
	html_context->was_xml_parsed = options->was_xml_parsed;

	/* FIXME (bug 784): cp is the terminal charset;
	 * should use the document charset instead.  */
	scan_http_equiv(start, end, head, title, options->cp);

	e = (struct html_element *)mem_calloc(1, sizeof(*e));
	if (!e) return NULL;
	add_to_list(html_context->stack, e);

	elformat.style.attr = 0;
	elformat.fontsize = 3;
	elformat.link = elformat.target = elformat.image = NULL;
	elformat.onclick = elformat.ondblclick = elformat.onmouseover = elformat.onhover
		= elformat.onfocus = elformat.onmouseout = elformat.onblur
		= elformat.onkeydown = elformat.onkeyup = elformat.onkeypress = NULL;
	elformat.select = NULL;
	elformat.form = NULL;
	elformat.title = NULL;

	elformat.style = options->default_style;
	elformat.color.clink = options->default_color.link;
	elformat.color.vlink = options->default_color.vlink;
#ifdef CONFIG_BOOKMARKS
	elformat.color.bookmark_link = options->default_color.bookmark_link;
#endif
	elformat.color.image_link = options->default_color.image_link;
	elformat.color.link_number = options->default_color.link_number;

	par_elformat.align = ALIGN_LEFT;
	par_elformat.leftmargin = 0; //options->margin;
	par_elformat.rightmargin = 0; //options->margin;

	par_elformat.width = options->document_width;
	par_elformat.list_level = par_elformat.list_number = 0;
	par_elformat.dd_margin = 0; //options->margin;
	par_elformat.flags = P_DISC;

	par_elformat.color.background = options->default_style.color.background;

	html_top->invisible = 0;
	html_top->name = NULL;
	html_top->namelen = 0;
	html_top->options = NULL;
	html_top->linebreak = 1;
	html_top->type = ELEMENT_DONT_KILL;

	html_context->has_link_lines = 0;
	html_context->table_level = 0;

#ifdef CONFIG_CSS
#ifdef CONFIG_LIBCSS
	if (options->libcss_enable) {
		if (options->css_enable) {
			css_error code;

			init_list(html_context->sheets);
			/* prepare a selection context containing the stylesheet */
			code = css_select_ctx_create(&html_context->select_ctx);
			if (code != CSS_OK) {
				//fprintf(stderr, "css_select_ctx_create code=%d\n", code);
			}
		}
	} else 
#endif
	do {
		html_context->css_styles.import_data = html_context;

		if (options->css_enable)
			mirror_css_stylesheet(&default_stylesheet,
				&html_context->css_styles);
	} while (0);
#endif

	return html_context;
}

void
done_html_parser(struct html_context *html_context)
{
	ELOG
#ifdef CONFIG_CSS
#ifdef CONFIG_LIBCSS
	if (html_context->options->libcss_enable) {
		if (html_context->options->css_enable) {
			struct el_sheet *el;

			(void)css_select_ctx_destroy(html_context->select_ctx);
			foreach (el, html_context->sheets) {
				(void)css_stylesheet_destroy(el->sheet);
			}
			free_list(html_context->sheets);
		}
	} else
#endif
	do {
		if (html_context->options->css_enable)
			done_css_stylesheet(&html_context->css_styles);
	} while (0);
#endif
	mem_free(html_context->base_target);
	done_uri(html_context->base_href);

	kill_html_stack_item(html_context, (struct html_element *)html_context->stack.next);

	assertm(list_empty(html_context->stack),
		"html stack not empty after operation");
	if_assert_failed init_list(html_context->stack);

	mem_free(html_context);
}
