/* The QuickJS CSSStyleDeclaration object implementation. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_LIBDOM
#include <dom/dom.h>
#include <dom/bindings/hubbub/parser.h>
#endif

#include "elinks.h"

#include "document/libdom/corestrings.h"
#include "js/ecmascript.h"
#include "js/quickjs/mapa.h"
#include "js/quickjs.h"
#include "js/quickjs/css.h"
#include "js/quickjs/element.h"

#define countof(x) (sizeof(x) / sizeof((x)[0]))

#if 0
static void *
js_CSSStyleDeclaration_GetOpaque(JSValueConst this_val)
{
	ELOG
	REF_JS(this_val);

	return attr_find_in_map_rev(map_rev_csses, this_val);
}
#endif

#if 0
static void
js_CSSStyleDeclaration_SetOpaque(JSValueConst this_val, void *node)
{
	ELOG
	REF_JS(this_val);

	if (!node) {
		attr_erase_from_map_rev(map_rev_csses, this_val);
	} else {
		attr_save_in_map_rev(map_rev_csses, this_val, node);
	}
}
#endif

static JSValue
js_CSSStyleDeclaration_get_property_length(JSContext *ctx, JSValueConst this_val)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	return JS_NewInt32(ctx, 3); // fake
}

static JSValue
js_CSSStyleDeclaration_getPropertyValue(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	return JS_UNDEFINED;
}

#if 0
static JSValue
js_CSSStyleDeclaration_item2(JSContext *ctx, JSValueConst this_val, int idx)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	return JS_NewString(ctx, "0"); // fake
}
#endif

static JSValue
js_CSSStyleDeclaration_item(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	if (argc != 1) {
		return JS_UNDEFINED;
	}
	return JS_NewString(ctx, "0"); // fake
}

#if 0
static JSValue
js_CSSStyleDeclaration_namedItem2(JSContext *ctx, JSValueConst this_val, const char *str)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);
	return JS_NewString(ctx, "0"); // fake
}
#endif

static JSValue
js_CSSStyleDeclaration_namedItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	if (argc != 1) {
		return JS_UNDEFINED;
	}
	return JS_NewString(ctx, "0"); // fake
}

static void
js_CSSStyleDeclaration_set_items(JSContext *ctx, JSValue this_val, void *node)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	JS_DefinePropertyValueStr(ctx, this_val, "marginTop", JS_NewString(ctx, "0"), JS_PROP_ENUMERABLE);
	JS_DefinePropertyValueStr(ctx, this_val, "marginLeft", JS_NewString(ctx, "0"), JS_PROP_ENUMERABLE);
	JS_DefinePropertyValueStr(ctx, this_val, "marginRight", JS_NewString(ctx, "0"), JS_PROP_ENUMERABLE);
}

static JSValue
js_CSSStyleDeclaration_toString(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	REF_JS(this_val);

	return JS_NewString(ctx, "[CSSStyleDeclaration object]");
}

static const JSCFunctionListEntry js_CSSStyleDeclaration_proto_funcs[] = {
	JS_CGETSET_DEF("length", js_CSSStyleDeclaration_get_property_length, NULL),
	JS_CFUNC_DEF("getPropertyValue", 1, js_CSSStyleDeclaration_getPropertyValue),
	JS_CFUNC_DEF("item", 1, js_CSSStyleDeclaration_item),
	JS_CFUNC_DEF("namedItem", 1, js_CSSStyleDeclaration_namedItem),
	JS_CFUNC_DEF("toString", 0, js_CSSStyleDeclaration_toString)
};

JSValue
getCSSStyleDeclaration(JSContext *ctx, void *node)
{
	ELOG
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif

	JSValue CSSStyleDeclaration_obj = JS_NewArray(ctx);
	JS_SetPropertyFunctionList(ctx, CSSStyleDeclaration_obj, js_CSSStyleDeclaration_proto_funcs, countof(js_CSSStyleDeclaration_proto_funcs));
	js_CSSStyleDeclaration_set_items(ctx, CSSStyleDeclaration_obj, node);

	RETURN_JS(CSSStyleDeclaration_obj);
}
