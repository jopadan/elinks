#ifndef EL__JS_MUJS_COLLECTION_H
#define EL__JS_MUJS_COLLECTION_H

#include <mujs.h>

#ifdef __cplusplus
extern "C" {
#endif

void mjs_push_collection(js_State *J, void *node);
void mjs_push_collection2(js_State *J, void *node);

#ifdef __cplusplus
}
#endif

#endif