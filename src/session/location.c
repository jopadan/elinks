/** Locations handling
 * @file */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "elinks.h"

#include "session/location.h"
#include "session/session.h"
#include "util/memory.h"
#include "util/string.h"

/** @relates location */
void
copy_location(struct location *dst, struct location *src)
{
	ELOG
	struct frame *frame, *new_frame;
	struct frame *iframe, *new_iframe;

	init_list(dst->frames);
	init_list(dst->iframes);
	foreachback (frame, src->frames) {
		new_frame = (struct frame *)mem_calloc(1, sizeof(*new_frame));
		if (new_frame) {
			new_frame->name = stracpy(frame->name);
			if (!new_frame->name) {
				mem_free(new_frame);
				return;
			}
			new_frame->redirect_cnt = 0;
			copy_vs(&new_frame->vs, &frame->vs);
			add_to_list(dst->frames, new_frame);
		}
	}

	foreachback (iframe, src->iframes) {
		new_iframe = (struct frame *)mem_calloc(1, sizeof(*new_iframe));
		if (new_iframe) {
			new_iframe->name = stracpy(iframe->name);
			if (!new_iframe->name) {
				mem_free(new_iframe);
				return;
			}
			new_iframe->redirect_cnt = 0;
			copy_vs(&new_iframe->vs, &iframe->vs);
			add_to_list(dst->iframes, new_iframe);
		}
	}

	copy_vs(&dst->vs, &src->vs);
}

void
destroy_location(struct location *loc)
{
	ELOG
	struct frame *frame;
	struct frame *iframe;

	foreach (frame, loc->frames) {
		destroy_vs(&frame->vs, 1);
		mem_free(frame->name);
	}

	foreach (iframe, loc->iframes) {
		destroy_vs(&iframe->vs, 1);
		mem_free(iframe->name);
	}

	free_list(loc->frames);
	free_list(loc->iframes);
	destroy_vs(&loc->vs, 1);
	mem_free(loc);
}
