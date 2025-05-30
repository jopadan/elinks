/* The SpiderMonkey html Text objects implementation. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elinks.h"

#include "js/libdom/dom.h"

#include "js/spidermonkey/util.h"
#include <jsfriendapi.h>

#include "bfu/dialog.h"
#include "cache/cache.h"
#include "cookies/cookies.h"
#include "dialogs/menu.h"
#include "dialogs/status.h"
#include "document/html/frames.h"
#include "document/document.h"
#include "document/forms.h"
#include "document/libdom/corestrings.h"
#include "document/libdom/doc.h"
#include "document/libdom/mapa.h"
#include "document/libdom/renderer2.h"
#include "document/view.h"
#include "js/ecmascript.h"
#include "js/ecmascript-c.h"
#include "js/spidermonkey/attr.h"
#include "js/spidermonkey/attributes.h"
#include "js/spidermonkey/collection.h"
#include "js/spidermonkey/dataset.h"
#include "js/spidermonkey/domrect.h"
#include "js/spidermonkey/event.h"
#include "js/spidermonkey/element.h"
#include "js/spidermonkey/heartbeat.h"
#include "js/spidermonkey/keyboard.h"
#include "js/spidermonkey/node.h"
#include "js/spidermonkey/nodelist.h"
#include "js/spidermonkey/nodelist2.h"
#include "js/spidermonkey/style.h"
#include "js/spidermonkey/text.h"
#include "js/spidermonkey/tokenlist.h"
#include "js/spidermonkey/window.h"
#include "intl/libintl.h"
#include "main/select.h"
#include "osdep/newwin.h"
#include "osdep/sysname.h"
#include "protocol/http/http.h"
#include "protocol/uri.h"
#include "session/history.h"
#include "session/location.h"
#include "session/session.h"
#include "session/task.h"
#include "terminal/tab.h"
#include "terminal/terminal.h"
#include "util/conv.h"
#include "util/memory.h"
#include "util/string.h"
#include "viewer/text/draw.h"
#include "viewer/text/form.h"
#include "viewer/text/link.h"
#include "viewer/text/vs.h"

#include <iostream>
#include <algorithm>
#include <map>
#include <string>

static bool text_get_property_children(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_childElementCount(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_childNodes(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_firstChild(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_firstElementChild(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_lastChild(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_lastElementChild(JSContext *ctx, unsigned int argc, JS::Value *vp);
//static bool text_get_property_nextElementSibling(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_nextSibling(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_nodeName(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_nodeType(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_nodeValue(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_set_property_nodeValue(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_ownerDocument(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_parentElement(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_parentNode(JSContext *ctx, unsigned int argc, JS::Value *vp);
//static bool text_get_property_previousElementSibling(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_previousSibling(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_get_property_tagName(JSContext *ctx, unsigned int argc, JS::Value *vp);

static bool text_get_property_textContent(JSContext *ctx, unsigned int argc, JS::Value *vp);
static bool text_set_property_textContent(JSContext *ctx, unsigned int argc, JS::Value *vp);

struct text_listener {
	LIST_HEAD_EL(struct text_listener);
	char *typ;
	JS::Heap<JS::Value> *fun;
};

struct text_private {
	LIST_OF(struct text_listener) listeners;
	struct ecmascript_interpreter *interpreter;
	JS::Heap<JSObject *> thisval;
	dom_event_listener *listener;
	dom_node *node;
	int ref_count;
};

//static std::map<void *, struct text_private *> map_privates;

static void text_finalize(JS::GCContext *op, JSObject *obj);
static void text_event_handler(dom_event *event, void *pw);

JSClassOps spi_text_ops = {
	nullptr,  // addProperty
	nullptr,  // deleteProperty
	nullptr,  // enumerate
	nullptr,  // newEnumerate
	nullptr,  // resolve
	nullptr,  // mayResolve
	text_finalize,  // finalize
	nullptr,  // call
	nullptr,  // construct
	JS_GlobalObjectTraceHook
};

JSClass text_class = {
	"Text",
	JSCLASS_HAS_RESERVED_SLOTS(2),
	&spi_text_ops
};

JSPropertySpec text_props[] = {
	JS_PSG("children",	text_get_property_children, JSPROP_ENUMERATE),
	JS_PSG("childElementCount",	text_get_property_childElementCount, JSPROP_ENUMERATE),
	JS_PSG("childNodes",	text_get_property_childNodes, JSPROP_ENUMERATE),
	JS_PSG("firstChild",	text_get_property_firstChild, JSPROP_ENUMERATE),
	JS_PSG("firstElementChild",	text_get_property_firstElementChild, JSPROP_ENUMERATE),
	JS_PSG("lastChild",	text_get_property_lastChild, JSPROP_ENUMERATE),
	JS_PSG("lastElementChild",	text_get_property_lastElementChild, JSPROP_ENUMERATE),
	JS_PSG("nextSibling",	text_get_property_nextSibling, JSPROP_ENUMERATE),
	JS_PSG("nodeName",	text_get_property_nodeName, JSPROP_ENUMERATE),
	JS_PSG("nodeType",	text_get_property_nodeType, JSPROP_ENUMERATE),
	JS_PSGS("nodeValue",	text_get_property_nodeValue, text_set_property_nodeValue, JSPROP_ENUMERATE),
	JS_PSG("ownerDocument",	text_get_property_ownerDocument, JSPROP_ENUMERATE),
	JS_PSG("parentElement",	text_get_property_parentElement, JSPROP_ENUMERATE),
	JS_PSG("parentNode",	text_get_property_parentNode, JSPROP_ENUMERATE),
////	JS_PSG("previousElementSibling",	text_get_property_previousElementSibling, JSPROP_ENUMERATE),
	JS_PSG("previousSibling",	text_get_property_previousSibling, JSPROP_ENUMERATE),
	JS_PSG("tagName",	text_get_property_tagName, JSPROP_ENUMERATE),
	JS_PSGS("textContent",	text_get_property_textContent, text_set_property_textContent, JSPROP_ENUMERATE),
	JS_PS_END
};

static void text_finalize(JS::GCContext *op, JSObject *obj)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(obj, 0);
	NODEINFO(el);

	struct text_private *el_private = JS::GetMaybePtrFromReservedSlot<struct text_private>(obj, 1);

	if (el_private) {
		if (--el_private->ref_count <= 0) {
			//map_privates.erase(el);

			if (el_private->listener) {
				dom_event_listener_unref(el_private->listener);
			}

			struct text_listener *l;

			foreach(l, el_private->listeners) {
				mem_free_set(&l->typ, NULL);
				delete (l->fun);
			}
			free_list(el_private->listeners);
			mem_free(el_private);
			JS::SetReservedSlot(obj, 1, JS::UndefinedValue());
		}
	}

	dom_node_unref(el);
	JS::SetReservedSlot(obj, 0, JS::UndefinedValue());
}


static bool
text_get_property_children(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_nodelist *nodes = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_child_nodes(el, &nodes);

	if (exc != DOM_NO_ERR || !nodes) {
		args.rval().setNull();
		return true;
	}
	JSObject *obj = getNodeList(ctx, nodes);
	args.rval().setObject(*obj);

	return true;
}

static bool
text_get_property_childElementCount(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_nodelist *nodes = NULL;
	dom_exception exc;
	uint32_t res = 0;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_child_nodes(el, &nodes);

	if (exc != DOM_NO_ERR || !nodes) {
		args.rval().setNull();
		return true;
	}
	exc = dom_nodelist_get_length(nodes, &res);
	dom_nodelist_unref(nodes);
	args.rval().setInt32(res);

	return true;
}

static bool
text_get_property_childNodes(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_nodelist *nodes = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_child_nodes(el, &nodes);

	if (exc != DOM_NO_ERR || !nodes) {
		args.rval().setNull();
		return true;
	}
	JSObject *obj = getNodeList(ctx, nodes);
	args.rval().setObject(*obj);

	return true;
}


static bool
text_get_property_firstChild(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *node = NULL;
	dom_exception exc;

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_first_child(el, &node);

	if (exc != DOM_NO_ERR || !node) {
		args.rval().setNull();
		return true;
	}

	JSObject *elem = getNode(ctx, node);
	dom_node_unref(node);
	args.rval().setObject(*elem);

	return true;
}

static bool
text_get_property_firstElementChild(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_nodelist *nodes = NULL;
	dom_exception exc;
	uint32_t size = 0;
	uint32_t i;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_child_nodes(el, &nodes);

	if (exc != DOM_NO_ERR || !nodes) {
		args.rval().setNull();
		return true;
	}
	exc = dom_nodelist_get_length(nodes, &size);

	if (exc != DOM_NO_ERR || !size) {
		dom_nodelist_unref(nodes);
		args.rval().setNull();
		return true;
	}

	for (i = 0; i < size; i++) {
		dom_node *child = NULL;
		exc = dom_nodelist_item(nodes, i, &child);
		dom_node_type type;

		if (exc != DOM_NO_ERR || !child) {
			continue;
		}

		exc = dom_node_get_node_type(child, &type);

		if (exc == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
			dom_nodelist_unref(nodes);
			JSObject *elem = getNode(ctx, child);
			dom_node_unref(child);
			args.rval().setObject(*elem);
			return true;
		}
		dom_node_unref(child);
	}
	dom_nodelist_unref(nodes);
	args.rval().setNull();

	return true;
}


static bool
text_get_property_lastChild(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *last_child = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_last_child(el, &last_child);

	if (exc != DOM_NO_ERR || !last_child) {
		args.rval().setNull();
		return true;
	}

	JSObject *elem = getNode(ctx, last_child);
	dom_node_unref(last_child);
	args.rval().setObject(*elem);

	return true;
}

static bool
text_get_property_lastElementChild(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_nodelist *nodes = NULL;
	dom_exception exc;
	uint32_t size = 0;
	int i;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_child_nodes(el, &nodes);

	if (exc != DOM_NO_ERR || !nodes) {
		args.rval().setNull();
		return true;
	}
	exc = dom_nodelist_get_length(nodes, &size);

	if (exc != DOM_NO_ERR || !size) {
		dom_nodelist_unref(nodes);
		args.rval().setNull();
		return true;
	}

	for (i = size - 1; i >= 0 ; i--) {
		dom_node *child = NULL;
		exc = dom_nodelist_item(nodes, i, &child);
		dom_node_type type;

		if (exc != DOM_NO_ERR || !child) {
			continue;
		}
		exc = dom_node_get_node_type(child, &type);

		if (exc == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
			dom_nodelist_unref(nodes);
			JSObject *elem = getNode(ctx, child);
			dom_node_unref(child);
			args.rval().setObject(*elem);
			return true;
		}
		dom_node_unref(child);
	}
	dom_nodelist_unref(nodes);
	args.rval().setNull();

	return true;
}

#if 0
static bool
text_get_property_nextElementSibling(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node;
	dom_node *prev_next = NULL;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	node = el;

	while (true) {
		dom_node *next = NULL;
		dom_exception exc = dom_node_get_next_sibling(node, &next);
		dom_node_type type;

		dom_node_unref(prev_next);

		if (exc != DOM_NO_ERR || !next) {
			args.rval().setNull();
			return true;
		}
		exc = dom_node_get_node_type(next, &type);

		if (exc == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
			JSObject *elem = getNode(ctx, next);
			dom_node_unref(next);
			args.rval().setObject(*elem);
			return true;
		}
		prev_next = next;
		node = next;
	}
	args.rval().setNull();

	return true;
}
#endif

static bool
text_get_property_nodeName(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *node = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(node);

	dom_string *name = NULL;
	dom_exception exc;

	if (!node) {
		args.rval().setString(JS_NewStringCopyZ(ctx, ""));
		return true;
	}
	exc = dom_node_get_node_name(node, &name);

	if (exc != DOM_NO_ERR || !name) {
		args.rval().setString(JS_NewStringCopyZ(ctx, ""));
		return true;
	}
	args.rval().setString(JS_NewStringCopyZ(ctx, dom_string_data(name)));
	dom_string_unref(name);

	return true;
}

static bool
text_get_property_nodeType(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *node = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(node);

	dom_node_type type1;
	dom_exception exc;

	if (!node) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_node_type(node, &type1);

	if (exc == DOM_NO_ERR) {
		args.rval().setInt32(type1);
		return true;
	}
	args.rval().setNull();

	return true;
}

static bool
text_get_property_nodeValue(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *node = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(node);

	dom_string *content = NULL;
	dom_exception exc;

	if (!node) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_node_value(node, &content);

	if (exc != DOM_NO_ERR || !content) {
		args.rval().setNull();
		return true;
	}
	args.rval().setString(JS_NewStringCopyZ(ctx, dom_string_data(content)));
	dom_string_unref(content);

	return true;
}

static bool
text_set_property_nodeValue(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *node = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(node);

	args.rval().setUndefined();

	if (!node) {
		return true;
	}
	char *str = jsval_to_string(ctx, args[0]);

	if (!str) {
		return false;
	}
	dom_string *value = NULL;
	dom_exception exc = dom_string_create((const uint8_t *)str, strlen(str), &value);
	mem_free(str);

	if (exc != DOM_NO_ERR || !value) {
		return true;
	}
	exc = dom_node_set_node_value(node, value);
	dom_string_unref(value);
	interpreter->changed = true;

	return true;
}

static bool
text_get_property_nextSibling(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_next_sibling(el, &node);

	if (exc != DOM_NO_ERR || !node) {
		args.rval().setNull();
		return true;
	}
	JSObject *elem = getNode(ctx, node);
	dom_node_unref(node);
	args.rval().setObject(*elem);

	return true;
}

static bool
text_get_property_ownerDocument(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	args.rval().setObject(*(JSObject *)(interpreter->document_obj));

	return true;
}

static bool
text_get_property_parentElement(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_parent_node(el, &node);

	if (exc != DOM_NO_ERR || !node) {
		args.rval().setNull();
		return true;
	}
	JSObject *elem = getNode(ctx, node);
	dom_node_unref(node);
	args.rval().setObject(*elem);

	return true;
}

static bool
text_get_property_parentNode(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_parent_node(el, &node);

	if (exc != DOM_NO_ERR || !node) {
		args.rval().setNull();
		return true;
	}
	JSObject *elem = getNode(ctx, node);
	dom_node_unref(node);
	args.rval().setObject(*elem);

	return true;
}

#if 0
static bool
text_get_property_previousElementSibling(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node;
	dom_node *prev_prev = NULL;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	node = el;

	while (true) {
		dom_node *prev = NULL;
		dom_exception exc = dom_node_get_previous_sibling(node, &prev);
		dom_node_type type;

		dom_node_unref(prev_prev);

		if (exc != DOM_NO_ERR || !prev) {
			args.rval().setNull();
			return true;
		}
		exc = dom_node_get_node_type(prev, &type);

		if (exc == DOM_NO_ERR && type == DOM_ELEMENT_NODE) {
			JSObject *elem = getNode(ctx, prev);
			dom_node_unref(prev);
			args.rval().setObject(*elem);
			return true;
		}
		prev_prev = prev;
		node = prev;
	}
	args.rval().setNull();

	return true;
}
#endif

static bool
text_get_property_previousSibling(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *node = NULL;
	dom_exception exc;

	if (!el) {
		args.rval().setNull();
		return true;
	}
	exc = dom_node_get_previous_sibling(el, &node);

	if (exc != DOM_NO_ERR || !node) {
		args.rval().setNull();
		return true;
	}
	JSObject *elem = getNode(ctx, node);
	dom_node_unref(node);
	args.rval().setObject(*elem);

	return true;
}

static bool
text_get_property_tagName(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	vs = interpreter->vs;
	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	dom_string *tag_name = NULL;
	dom_exception exc = dom_node_get_node_name(el, &tag_name);

	if (exc != DOM_NO_ERR || !tag_name) {
		args.rval().setNull();
		return true;
	}
	args.rval().setString(JS_NewStringCopyZ(ctx, dom_string_data(tag_name)));
	dom_string_unref(tag_name);

	return true;
}

static bool
text_get_property_textContent(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct view_state *vs;
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);

	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	vs = interpreter->vs;

	if (!vs) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	dom_string *content = NULL;
	dom_exception exc = dom_node_get_text_content(el, &content);

	if (exc != DOM_NO_ERR || !content) {
		args.rval().setNull();
		return true;
	}
	args.rval().setString(JS_NewStringCopyZ(ctx, dom_string_data(content)));
	dom_string_unref(content);

	return true;
}





static bool
text_set_property_textContent(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	/* This can be called if @obj if not itself an instance of the
	 * appropriate class but has one in its prototype chain.  Fail
	 * such calls.  */
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setUndefined();
		return true;
	}
	char *str = jsval_to_string(ctx, args[0]);

	if (!str) {
		return false;
	}
	dom_string *content = NULL;
	dom_exception exc = dom_string_create((const uint8_t *)str, strlen(str), &content);
	mem_free(str);

	if (exc != DOM_NO_ERR || !content) {
		return false;
	}
	exc = dom_node_set_text_content(el, content);
	dom_string_unref(content);
	args.rval().setUndefined();

	return true;
}

static bool text_addEventListener(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_appendChild(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_cloneNode(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_contains(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_dispatchEvent(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_hasChildNodes(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_insertBefore(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_isEqualNode(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_isSameNode(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_querySelector(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_querySelectorAll(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_removeChild(JSContext *ctx, unsigned int argc, JS::Value *rval);
static bool text_removeEventListener(JSContext *ctx, unsigned int argc, JS::Value *rval);

const spidermonkeyFunctionSpec text_funcs[] = {
	{ "addEventListener",	text_addEventListener,	3 },
	{ "appendChild",	text_appendChild,	1 },
	{ "cloneNode",	text_cloneNode,	1 },
	{ "contains",	text_contains,	1 },
	{ "dispatchEvent", text_dispatchEvent,	1 },
	{ "hasChildNodes",		text_hasChildNodes,	0 },
	{ "insertBefore",		text_insertBefore,	2 },
	{ "isEqualNode",		text_isEqualNode,	1 },
	{ "isSameNode",			text_isSameNode,	1 },
	{ "querySelector",		text_querySelector,	1 },
	{ "querySelectorAll",		text_querySelectorAll,	1 },
	{ "removeChild",	text_removeChild,	1 },
	{ "removeEventListener",	text_removeEventListener,	3 },
	{ NULL }
};

#if 0
// Common part of all add_child_element*() methods.
static xmlpp::Element*
el_add_child_text_common(xmlNode* child, xmlNode* node)
{
	if (!node) {
		xmlFreeNode(child);
		throw xmlpp::internal_error("Could not add child element node");
	}
	xmlpp::Node::create_wrapper(node);

	return static_cast<xmlpp::Element*>(node->_private);
}
#endif

static bool
text_addEventListener(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	struct text_private *el_private = JS::GetMaybePtrFromReservedSlot<struct text_private>(hobj, 1);

	if (!el || !el_private) {
		args.rval().setNull();
		return true;
	}

	if (argc < 2) {
		args.rval().setUndefined();
		return true;
	}
	char *method = jsval_to_string(ctx, args[0]);
	JS::RootedValue fun(ctx, args[1]);
	struct text_listener *l;

	foreach(l, el_private->listeners) {
		if (strcmp(l->typ, method)) {
			continue;
		}
		if (*(l->fun) == fun) {
			mem_free(method);
			args.rval().setUndefined();
			return true;
		}
	}
	struct text_listener *n = (struct text_listener *)mem_calloc(1, sizeof(*n));

	if (!n) {
		args.rval().setUndefined();
		return false;
	}
	n->fun = new JS::Heap<JS::Value>(fun);
	n->typ = method;
	add_to_list_end(el_private->listeners, n);
	dom_exception exc;

	if (el_private->listener) {
		dom_event_listener_ref(el_private->listener);
	} else {
		exc = dom_event_listener_create(text_event_handler, el_private, &el_private->listener);

		if (exc != DOM_NO_ERR || !el_private->listener) {
			args.rval().setUndefined();
			return true;
		}
	}
	dom_string *typ = NULL;
	exc = dom_string_create((const uint8_t *)method, strlen(method), &typ);

	if (exc != DOM_NO_ERR || !typ) {
		goto ex;
	}
	exc = dom_event_target_add_event_listener(el, typ, el_private->listener, false);

	if (exc == DOM_NO_ERR) {
		dom_event_listener_ref(el_private->listener);
	}

ex:
	dom_string_unref(typ);
	dom_event_listener_unref(el_private->listener);
	args.rval().setUndefined();

	return true;
}

static bool
text_removeEventListener(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	struct text_private *el_private = JS::GetMaybePtrFromReservedSlot<struct text_private>(hobj, 1);

	if (!el || !el_private) {
		args.rval().setNull();
		return true;
	}

	if (argc < 2) {
		args.rval().setUndefined();
		return true;
	}
	char *method = jsval_to_string(ctx, args[0]);

	if (!method) {
		return false;
	}
	JS::RootedValue fun(ctx, args[1]);

	struct text_listener *l;

	foreach(l, el_private->listeners) {
		if (strcmp(l->typ, method)) {
			continue;
		}
		if (*(l->fun) == fun) {

			dom_string *typ = NULL;
			dom_exception exc = dom_string_create((const uint8_t *)method, strlen(method), &typ);

			if (exc != DOM_NO_ERR || !typ) {
				continue;
			}
			//dom_event_target_remove_event_listener(el, typ, el_private->listener, false);
			dom_string_unref(typ);

			del_from_list(l);
			mem_free_set(&l->typ, NULL);
			delete (l->fun);
			mem_free(l);
			mem_free(method);
			args.rval().setUndefined();
			return true;
		}
	}
	mem_free(method);
	args.rval().setUndefined();
	return true;
}

static bool
text_appendChild(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);
	struct view_state *vs = interpreter->vs;
	struct document_view *doc_view = vs->doc_view;
	struct document *document = doc_view->document;

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_node *res = NULL;

	if (argc != 1) {
		return false;
	}

	if (!el) {
		return false;
	}
	dom_node *el2 = NULL;

	if (!args[0].isNull()) {
		JS::RootedObject node(ctx, &args[0].toObject());
		el2 = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(node, 0);
	}

	if (!el2) {
		return false;
	}
	dom_exception exc = dom_node_append_child(el, el2, &res);

	if (exc == DOM_NO_ERR && res) {
		interpreter->changed = 1;
		JSObject *obj = getNode(ctx, res);
		dom_node_unref(res);
		args.rval().setObject(*obj);
		debug_dump_xhtml(document->dom);
		return true;
	}

	return false;
}


static bool
text_cloneNode(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	dom_exception exc;
	bool deep = args[0].toBoolean();
	dom_node *clone = NULL;
	exc = dom_node_clone_node(el, deep, &clone);

	if (exc != DOM_NO_ERR || !clone) {
		args.rval().setNull();
		return true;
	}
	JSObject *obj = getNode(ctx, clone);
	dom_node_unref(clone);
	args.rval().setObject(*obj);

	return true;
}
#if 0
static bool
isAncestor(dom_node *el, dom_node *node)
{
	dom_node *prev_next = NULL;
	while (node) {
		dom_exception exc;
		dom_node *next = NULL;
		dom_node_unref(prev_next);
		if (el == node) {
			return true;
		}
		exc = dom_node_get_parent_node(node, &next);
		if (exc != DOM_NO_ERR || !next) {
			break;
		}
		prev_next = next;
		node = next;
	}

	return false;
}
#endif


static bool
text_contains(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		return false;
	}
	dom_node *el2 = NULL;

	if (!args[0].isNull()) {
		JS::RootedObject node(ctx, &args[0].toObject());
		el2 = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(node, 0);
	}

	if (!el2) {
		return false;
	}
	dom_node_ref(el);
	dom_node_ref(el2);

	while (1) {
		if (el == el2) {
			dom_node_unref(el);
			dom_node_unref(el2);
			args.rval().setBoolean(true);
			return true;
		}
		dom_node *node = NULL;
		dom_exception exc = dom_node_get_parent_node(el2, &node);

		if (exc != DOM_NO_ERR || !node) {
			dom_node_unref(el);
			dom_node_unref(el2);
			args.rval().setBoolean(false);
			return true;
		}
		dom_node_unref(el2);
		el2 = node;
	}
}


static bool
text_hasChildNodes(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 0) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
		args.rval().setBoolean(false);
		return true;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	dom_exception exc;
	bool res;

	if (!el) {
		args.rval().setBoolean(false);
		return true;
	}
	exc = dom_node_has_child_nodes(el, &res);

	if (exc != DOM_NO_ERR) {
		args.rval().setBoolean(false);
		return true;
	}
	args.rval().setBoolean(res);

	return true;
}

static bool
text_insertBefore(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 2) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);
	struct view_state *vs = interpreter->vs;
	struct document_view *doc_view = vs->doc_view;
	struct document *document = doc_view->document;

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		return false;
	}
	dom_node *next_sibling = NULL;
	dom_node *child = NULL;

	if (!args[1].isNull()) {
		JS::RootedObject next_sibling1(ctx, &args[1].toObject());
		next_sibling = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(next_sibling1, 0);
	}

	if (!args[0].isNull()) {
		JS::RootedObject child1(ctx, &args[0].toObject());
		child = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(child1, 0);
	}

	if (!child) {
		return false;
	}
	dom_node *spare = NULL;
	dom_exception err = dom_node_insert_before(el, child, next_sibling, &spare);

	if (err != DOM_NO_ERR || !spare) {
		return false;
	}
	JSObject *obj = getNode(ctx, spare);
	dom_node_unref(spare);
	args.rval().setObject(*obj);
	interpreter->changed = 1;
	debug_dump_xhtml(document->dom);

	return true;
}

static bool
text_isEqualNode(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		return false;
	}
	dom_node *el2 = NULL;

	if (!args[0].isNull()) {
		JS::RootedObject node(ctx, &args[0].toObject());
		el2 = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(node, 0);
	}

	if (!el2) {
		return false;
	}

	struct string first;
	struct string second;

	if (!init_string(&first)) {
		return false;
	}
	if (!init_string(&second)) {
		done_string(&first);
		return false;
	}

	ecmascript_walk_tree(&first, el, false, true);
	ecmascript_walk_tree(&second, el2, false, true);

	bool ret = !strcmp(first.source, second.source);

	done_string(&first);
	done_string(&second);
	args.rval().setBoolean(ret);

	return true;
}

static bool
text_isSameNode(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		return false;
	}
	dom_node *el2 = NULL;

	if (!args[0].isNull()) {
		JS::RootedObject node(ctx, &args[0].toObject());
		el2 = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(node, 0);
	}
	args.rval().setBoolean(el == el2);

	return true;
}

static bool
text_querySelector(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);

	if (argc != 1) {
		args.rval().setBoolean(false);
		return true;
	}

	JS::RootedObject hobj(ctx, &args.thisv().toObject());
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	char *selector = jsval_to_string(ctx, args[0]);

	if (!selector) {
		args.rval().setNull();
		return true;
	}
	void *ret = walk_tree_query(el, selector, 0);
	mem_free(selector);

	if (!ret) {
		args.rval().setNull();
	} else {
		JSObject *el = getNode(ctx, ret);
		dom_node_unref(ret);
		args.rval().setObject(*el);
	}

	return true;
}

static bool
text_querySelectorAll(JSContext *ctx, unsigned int argc, JS::Value *vp)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::CallArgs args = CallArgsFromVp(argc, vp);

	if (argc != 1) {
		args.rval().setBoolean(false);
		return true;
	}

	JS::RootedObject hobj(ctx, &args.thisv().toObject());
	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	JS::Realm *comp = js::GetContextRealm(ctx);
	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);
	struct document_view *doc_view = interpreter->vs->doc_view;
	struct document *document = doc_view->document;

	if (!document->dom) {
		args.rval().setNull();
		return true;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el) {
		args.rval().setNull();
		return true;
	}
	char *selector = jsval_to_string(ctx, args[0]);

	if (!selector) {
		args.rval().setNull();
		return true;
	}
	LIST_OF(struct selector_node) *result_list = (LIST_OF(struct selector_node) *)mem_calloc(1, sizeof(*result_list));

	if (!result_list) {
		mem_free(selector);
		args.rval().setNull();
		return true;
	}
	init_list(*result_list);
	walk_tree_query_append(el, selector, 0, result_list);
	mem_free(selector);
	JSObject *obj = getNodeList2(ctx, result_list);
	args.rval().setObject(*obj);

	return true;
}

static bool
text_removeChild(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp || argc != 1) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);
	struct view_state *vs = interpreter->vs;
	struct document_view *doc_view = vs->doc_view;
	struct document *document = doc_view->document;

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *el = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(el);

	if (!el || !args[0].isObject()) {
		args.rval().setNull();
		return true;
	}
	JS::RootedObject node(ctx, &args[0].toObject());
	dom_node *el2 = (dom_node *)JS::GetMaybePtrFromReservedSlot<dom_node>(node, 0);
	dom_exception exc;
	dom_node *spare = NULL;
	exc = dom_node_remove_child(el, el2, &spare);

	if (exc == DOM_NO_ERR && spare) {
		interpreter->changed = 1;
		JSObject *obj = getNode(ctx, spare);
		dom_node_unref(spare);
		args.rval().setObject(*obj);
		debug_dump_xhtml(document->dom);
		return true;
	}
	args.rval().setNull();

	return true;
}

JSObject *
getText(JSContext *ctx, void *node)
{
	struct text_private *el_private = (struct text_private *)mem_calloc(1, sizeof(*el_private));

	if (!el_private) {
		return NULL;
	}
	init_list(el_private->listeners);
	el_private->ref_count = 1;
	el_private->node = (dom_node *)node;

	JSObject *el = JS_NewObject(ctx, &text_class);

	if (!el) {
		mem_free(el_private);
		return NULL;
	}
	JS::Realm *comp = js::GetContextRealm(ctx);
	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)JS::GetRealmPrivate(comp);
	el_private->interpreter = interpreter;

	JS::RootedObject r_el(ctx, el);

	JS_DefineProperties(ctx, r_el, (JSPropertySpec *) text_props);
	spidermonkey_DefineFunctions(ctx, el, text_funcs);

	JS::SetReservedSlot(el, 0, JS::PrivateValue(node));
	JS::SetReservedSlot(el, 1, JS::PrivateValue(el_private));

	el_private->thisval = r_el;
	dom_node_ref((dom_node *)node);

	return el;
}

#if 0
void
check_text_event(void *interp, void *elem, const char *event_name, struct term_event *ev)
{
	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)interp;
	JSContext *ctx = (JSContext *)interpreter->backend_data;
	JSObject *obj;
	auto el = map_privates.find(elem);

	if (el == map_privates.end()) {
		return;
	}
	struct text_private *el_private = el->second;
	JSAutoRealm ar(ctx, (JSObject *)interpreter->ac->get());
	JS::RootedValue r_val(ctx);
	interpreter->heartbeat = add_heartbeat(interpreter);

	struct text_listener *l;

	foreach(l, el_private->listeners) {
		if (strcmp(l->typ, event_name)) {
			continue;
		}
		if (ev && ev->ev == EVENT_KBD && (!strcmp(event_name, "keydown") || !strcmp(event_name, "keyup") || !strcmp(event_name, "keypress"))) {
			JS::RootedValueArray<1> argv(ctx);
			obj = get_keyboardEvent(ctx, ev);
			argv[0].setObject(*obj);
			JS::RootedValue r_val(ctx);
			JS::RootedObject thisv(ctx, el_private->thisval);
			JS::RootedValue vfun(ctx, *(l->fun));
			JS_CallFunctionValue(ctx, thisv, vfun, argv, &r_val);
		} else {
			JS::RootedValue r_val(ctx);
			JS::RootedObject thisv(ctx, el_private->thisval);
			JS::RootedValue vfun(ctx, *(l->fun));
			JS_CallFunctionValue(ctx, thisv, vfun, JS::HandleValueArray::empty(), &r_val);
		}
	}
	done_heartbeat(interpreter->heartbeat);

	check_for_rerender(interpreter, event_name);
}
#endif

static bool
text_dispatchEvent(JSContext *ctx, unsigned int argc, JS::Value *rval)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	JS::Realm *comp = js::GetContextRealm(ctx);

	if (!comp) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}

	JS::CallArgs args = CallArgsFromVp(argc, rval);
	JS::RootedObject hobj(ctx, &args.thisv().toObject());

	if (!JS_InstanceOf(ctx, hobj, &text_class, NULL)) {
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s %d\n", __FILE__, __FUNCTION__, __LINE__);
#endif
		return false;
	}
	dom_node *element = JS::GetMaybePtrFromReservedSlot<dom_node>(hobj, 0);
	NODEINFO(element);

	if (!element) {
		args.rval().setBoolean(false);
		return true;
	}

	if (argc < 1) {
		args.rval().setBoolean(false);
		return true;
	}
	JS::RootedObject eve(ctx, &args[0].toObject());
	dom_event *event = (dom_event *)JS::GetMaybePtrFromReservedSlot<dom_event>(eve, 0);
	bool result = false;
	(void)dom_event_target_dispatch_event(element, event, &result);
	args.rval().setBoolean(result);

	return true;
}

static void
text_event_handler(dom_event *event, void *pw)
{
#ifdef ECMASCRIPT_DEBUG
	fprintf(stderr, "%s:%s\n", __FILE__, __FUNCTION__);
#endif
	struct text_private *el_private = (struct text_private *)pw;
	struct ecmascript_interpreter *interpreter = (struct ecmascript_interpreter *)el_private->interpreter;
	JSContext *ctx = (JSContext *)interpreter->backend_data;
	JSAutoRealm ar(ctx, (JSObject *)interpreter->ac->get());
	JS::RootedValue r_val(ctx);

	if (!event) {
		return;
	}
	dom_string *typ = NULL;
	dom_exception exc = dom_event_get_type(event, &typ);

	if (exc != DOM_NO_ERR || !typ) {
		return;
	}
	interpreter->heartbeat = add_heartbeat(interpreter);
	JSObject *obj_ev = getEvent(ctx, event);
	interpreter->heartbeat = add_heartbeat(interpreter);

	struct text_listener *l, *next;

	foreachsafe(l, next, el_private->listeners) {
		if (strcmp(l->typ, dom_string_data(typ))) {
			continue;
		}
		JS::RootedValueArray<1> argv(ctx);
		argv[0].setObject(*obj_ev);
		JS::RootedValue r_val(ctx);
		JS::RootedObject thisv(ctx, el_private->thisval);
		JS::RootedValue vfun(ctx, *(l->fun));
		JS_CallFunctionValue(ctx, thisv, vfun, argv, &r_val);
	}
	done_heartbeat(interpreter->heartbeat);
	check_for_rerender(interpreter, dom_string_data(typ));
	dom_string_unref(typ);
}
