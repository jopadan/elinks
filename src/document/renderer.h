#ifndef EL__DOCUMENT_RENDERER_H
#define EL__DOCUMENT_RENDERER_H

#include "document/document.h"

#ifdef __cplusplus
extern "C" {
#endif

struct conv_table;
struct document_options;
struct document_view;
struct session;
struct view_state;
struct screen_char;

void render_document(struct view_state *, struct document_view *, struct document_options *);
void render_document_frames(struct session *ses, int no_cache);
struct conv_table *get_convert_table(char *head, int to_cp, int default_cp, int *from_cp, enum cp_status *cp_status, int ignore_server_cp);
void compress_empty_lines(struct document *document);
void sort_links(struct document *document);

#ifdef __cplusplus
}
#endif

#endif
