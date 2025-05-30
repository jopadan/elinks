
#ifndef EL__DOCUMENT_HTML_PARSER_H
#define EL__DOCUMENT_HTML_PARSER_H

#include "document/format.h"
#include "document/forms.h"
#include "document/html/renderer.h" /* enum html_special_type */
#include "intl/charsets.h" /* unicode_val_T */
#include "util/align.h"
#include "util/color.h"
#include "util/lists.h"

#ifdef __cplusplus
extern "C" {
#endif

struct document;
struct document_options;
struct el_form_control;
struct frameset_desc;
struct html_context;
struct memory_list;
struct menu_item;
struct part;
struct string;
struct uri;

/* XXX: This is just terible - this interface is from 75% only for other HTML
 * files - there's lack of any well defined interface and it's all randomly
 * mixed up :/. */
struct text_attrib_color {
	color_T clink;
	color_T vlink;
#ifdef CONFIG_BOOKMARKS
	color_T bookmark_link;
#endif
	color_T image_link;
	color_T link_number;
};

struct text_attrib {
	struct text_style style;

	int fontsize;
	char *link;
	char *target;
	char *image;

	/* Any entities in the title have already been decoded.  */
	char *title;

	struct el_form_control *form;

	struct text_attrib_color color;

#ifdef CONFIG_CSS
	/* Bug 766: CSS speedup.  56% of CPU time was going to
	 * get_attr_value().  Of those calls, 97% were asking for "id"
	 * or "class".  So cache the results.  start_element() sets up
	 * these pointers if html_context->options->css_enable;
	 * otherwise they remain NULL. */
	char *id;
	char *class_;
#endif

	char *select;
	form_mode_T select_disabled;
	unsigned int tabindex;
	unicode_val_T accesskey;

	char *onclick;
	char *ondblclick;
	char *onmouseover;
	char *onhover;
	char *onfocus;
	char *onmouseout;
	char *onblur;
	char *onkeydown;
	char *onkeyup;
	char *onkeypress;

	char *top_name;
};

/* This enum is pretty ugly, yes ;). */
enum format_list_flag {
	P_NO_BULLET = 0,

	P_NUMBER = 1,
	P_alpha = 2,
	P_ALPHA = 3,
	P_roman = 4,
	P_ROMAN = 5,

	P_DISC = 1,
	P_O = 2,
	P_SQUARE = 3,

	P_LISTMASK = 7,

	P_COMPACT = 8,
};

typedef unsigned char format_list_flag_T;

struct par_attrib {
	format_align_T align;
	int leftmargin;
	int rightmargin;
	int width;
	int list_level;
	int blockquote_level;
	int orig_leftmargin;
	unsigned list_number;
	int dd_margin;
	format_list_flag_T flags;
	struct {
		color_T background;
	} color;
};

/* HTML parser stack mortality info */
enum html_element_mortality_type {
	/* Elements of this type can not be removed from the stack. This type
	 * is created by the renderer when formatting an HTML part. */
	ELEMENT_IMMORTAL,
	/* Elements of this type can only be removed by elements of the start
	 * type. This type is created whenever an HTML state is created using
	 * init_html_parser_state(). */
	/* The element has been created by*/
	ELEMENT_DONT_KILL,
	/* These elements can safely be removed from the stack by both */
	ELEMENT_KILLABLE,
	/* These elements not only cannot bear any other elements inside but
	 * any attempt to do so will cause them to terminate. This is so deadly
	 * that it affects even invisible elements. Ie. <title>foo<body>. */
	ELEMENT_WEAK,
};

enum html_element_pseudo_class {
	ELEMENT_LINK = 1,
	ELEMENT_VISITED = 2,
};

typedef unsigned char html_element_pseudo_class_T;

struct html_element {
	LIST_HEAD_EL(struct html_element);

	enum html_element_mortality_type type;

	struct text_attrib attr;
	struct par_attrib parattr;

	/* invisible is a flag using which element handlers can control
	 * processing in start_element. 0 indicates that start_element should
	 * process tags, 1 indicates that it should not, and 2 or greater
	 * indicates that it should process only script tags. */
	int invisible;

	unsigned int visibility_hidden:1;

	/* The name of the element without NUL termination. name is a pointer
	 * into the actual document source. */
	char *name;
	int namelen;

	char *options;
	void *node;
	/* See document/html/parser/parse.c's element_info.linebreak
	 * description. */
	int linebreak;
	struct frameset_desc *frameset;

	/* For the needs of CSS engine. A wannabe bitmask. */
	html_element_pseudo_class_T pseudo_class;
};

#define is_inline_element(e) ((e)->linebreak == 0)
#define is_block_element(e) ((e)->linebreak > 0)

/* Interface for the renderer */

struct html_context *
init_html_parser(struct uri *uri, struct document *document,
		 char *start, char *end,
		 struct string *head, struct string *title,
		 void (*put_chars)(struct html_context *, const char *, int),
		 void (*line_break)(struct html_context *),
		 void *(*special)(struct html_context *, html_special_type_T,
		                  ...));
void done_html_parser(struct html_context *html_context);

void *init_html_parser_state(struct html_context *html_context, enum html_element_mortality_type type, int align, int margin, int width);
void done_html_parser_state(struct html_context *html_context, void *state);

/* Interface for the table handling */

int get_bgcolor(struct html_context *html_context, char *a, color_T *rgb);
void set_fragment_identifier(struct html_context *html_context,
                             char *attr_name, const char *attr);
void add_fragment_identifier(struct html_context *html_context,
                             struct part *, char *attr);

/* Interface for the viewer */

int
get_image_map(char *head, char *pos, char *eof,
	      struct menu_item **menu, struct memory_list **ml, struct uri *uri,
	      struct document_options *options, char *target_base,
	      int to, int def, int hdef);

/* For html/parser/forms.c,general.c,link.c,parse.c,stack.c */

/* Ensure that there are at least n successive line-breaks at the current
 * position, but don't add more than necessary to bring the current number
 * of successive line-breaks above n.
 *
 * For example, there should be two line-breaks after a <br>, but multiple
 * successive <br>'s warrant still only two line-breaks.  ln_break will be
 * called with n = 2 for each of multiple successive <br>'s, but ln_break
 * will only add two line-breaks for the entire run of <br>'s. */
void ln_break(struct html_context *html_context, int n);

int get_color(struct html_context *html_context, char *a, const char *c, color_T *rgb);

int get_color2(struct html_context *html_context, char *value_value, color_T *rgb);

#ifdef __cplusplus
}
#endif

#endif
