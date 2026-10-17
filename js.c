/* Cosmos Browser - JavaScript (QuickJS) y puente nativo hacia el árbol DOM del motor.
 * La mayor parte de la API web (eventos, temporizadores, fetch, URL, classList...) vive en prelude.js;
 * aquí solo están las primitivas que tocan el árbol de nodos, la red y la ejecución de scripts. */
#include "cosmos.h"
#include "quickjs/quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>

#include "prelude.h"          /* const char PRELUDE_JS[]; generado desde prelude.js */

typedef struct { Node *n; char *body; size_t len; char url[2048]; int rc; } ScriptJob;

typedef struct JS {
    JSRuntime *rt; JSContext *ctx; Doc *d;
    JSClassID cls;
    JSValue **wr; int nwr, cwr;
    double deadline;              /* ms monotónicos; 0 = sin límite */
    int nfetch, local, debug;
    char pageurl[2048];
    char *nav;                    /* navegación pedida por location.href = ... */
    ScriptJob *cache; int ncache;
    JSValue docv;
} JS;

static double mono_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static JS *jsof(JSContext *c) { return JS_GetContextOpaque(c); }

static int interrupt_cb(JSContext *cx, void *op) {
    JS *j = op; (void)cx;
    return j->deadline > 0 && mono_ms() > j->deadline;
}

static void js_log_exc(JS *j, const char *where) {
    JSValue e = JS_GetException(j->ctx);
    if (j->debug) {
        const char *s = JS_ToCString(j->ctx, e);
        JSValue st = JS_GetPropertyStr(j->ctx, e, "stack");
        const char *ss = JS_IsUndefined(st) ? NULL : JS_ToCString(j->ctx, st);
        fprintf(stderr, "[js] error en %s: %s%s%s\n", where, s ? s : "?", ss ? "\n" : "", ss ? ss : "");
        if (ss) JS_FreeCString(j->ctx, ss);
        JS_FreeValue(j->ctx, st);
        if (s) JS_FreeCString(j->ctx, s);
    }
    JS_FreeValue(j->ctx, e);
}

static void pump(JS *j) {
    JSContext *c;
    int r;
    while ((r = JS_ExecutePendingJob(j->rt, &c)) > 0) { }
    if (r < 0) js_log_exc(j, "promesa");
}

/* ---------- envoltorios ---------- */
static JSValue wrap(JS *j, Node *n) {
    if (!n) return JS_NULL;
    if (n->js) return JS_DupValue(j->ctx, *(JSValue *)n->js);
    JSValue o = JS_NewObjectClass(j->ctx, j->cls);
    JS_SetOpaque(o, n);
    JSValue *h = malloc(sizeof *h);
    *h = JS_DupValue(j->ctx, o);
    n->js = h;
    if (j->nwr == j->cwr) { j->cwr = j->cwr ? j->cwr * 2 : 256; j->wr = realloc(j->wr, j->cwr * sizeof *j->wr); }
    j->wr[j->nwr++] = h;
    return o;
}

#define GETNODE(c, v) Node *n = JS_GetOpaque(v, jsof(c)->cls); if (!n) return JS_ThrowTypeError(c, "no es un Node")
static Node *argnode(JSContext *c, JSValueConst v) { return JS_GetOpaque(v, jsof(c)->cls); }
static char *argstr(JSContext *c, JSValueConst v, size_t *len) {
    size_t l; const char *s = JS_ToCStringLen(c, &l, v);
    if (!s) { if (len) *len = 0; return strdup(""); }
    char *r = malloc(l + 1); memcpy(r, s, l); r[l] = 0;
    JS_FreeCString(c, s);
    if (len) *len = l;
    return r;
}
static JSValue str_or_null(JSContext *c, const char *s) { return s ? JS_NewString(c, s) : JS_NULL; }

/* ---------- operaciones DOM ---------- */
static void detach(Node *c) {
    Node *p = c->parent;
    if (!p) return;
    if (p->first == c) p->first = c->next;
    else { Node *q = p->first; while (q && q->next != c) q = q->next; if (q) q->next = c->next; if (p->last == c) p->last = q; }
    if (p->first == NULL) p->last = NULL;
    else if (p->last == c) { Node *q = p->first; while (q->next) q = q->next; p->last = q; }
    c->parent = NULL; c->next = NULL;
}

static int is_ancestor(Node *a, Node *n) { for (; n; n = n->parent) if (n == a) return 1; return 0; }

static void set_attr(Doc *d, Node *n, const char *name, const char *val) {
    for (int i = 0; i < n->nattrs; i++) if (!strcmp(n->attrs[i].name, name)) { n->attrs[i].val = arena_strndup(&d->arena, val, strlen(val)); return; }
    Attr *na = arena_alloc(&d->arena, sizeof(Attr) * (n->nattrs + 1));
    if (n->nattrs) memcpy(na, n->attrs, sizeof(Attr) * n->nattrs);
    na[n->nattrs].name = arena_strndup(&d->arena, name, strlen(name));
    na[n->nattrs].val = arena_strndup(&d->arena, val, strlen(val));
    n->attrs = na; n->nattrs++;
}
static void rm_attr(Node *n, const char *name) {
    for (int i = 0; i < n->nattrs; i++) if (!strcmp(n->attrs[i].name, name)) {
        for (int k = i; k + 1 < n->nattrs; k++) n->attrs[k] = n->attrs[k + 1];
        n->nattrs--; return;
    }
}

static Node *mk_node(Doc *d, int type, const char *name) {
    Node *n = arena_alloc(&d->arena, sizeof(Node));
    n->type = (uint8_t)type;
    n->name = arena_strndup(&d->arena, name, strlen(name));
    return n;
}

static Node *clone_node(Doc *d, Node *src, int deep) {
    Node *n = mk_node(d, src->type, src->name);
    if (src->type == 1) { n->text = arena_strndup(&d->arena, src->text, src->tlen); n->tlen = src->tlen; return n; }
    if (src->nattrs) {
        n->attrs = arena_alloc(&d->arena, sizeof(Attr) * src->nattrs);
        memcpy(n->attrs, src->attrs, sizeof(Attr) * src->nattrs);
        n->nattrs = src->nattrs;
    }
    if (deep) for (Node *c = src->first; c; c = c->next) {
        Node *cc = clone_node(d, c, 1);
        cc->parent = n;
        if (n->last) n->last->next = cc; else n->first = cc;
        n->last = cc;
    }
    return n;
}

static void text_rec(Node *n, char **buf, size_t *len, size_t *cap) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type == 1) {
            if (*len + c->tlen + 1 > *cap) { *cap = (*len + c->tlen + 1) * 2; *buf = realloc(*buf, *cap); }
            memcpy(*buf + *len, c->text, c->tlen); *len += c->tlen;
        } else if (strcmp(c->name, "script") && strcmp(c->name, "style")) text_rec(c, buf, len, cap);
    }
}

/* ---------- métodos de Node ---------- */
static JSValue n_type(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t);
    int ty = n->type == 1 ? 3 : (!strcmp(n->name, "#root") ? 9 : (n->name[0] == '#' ? 11 : 1));
    return JS_NewInt32(c, ty);
}
static JSValue n_name(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { GETNODE(c, t); return JS_NewString(c, n->name); }
static JSValue n_attr(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *k = argstr(c, av[0], NULL);
    const char *v = node_attr(n, k); free(k);
    return str_or_null(c, v);
}
static JSValue n_setattr(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *k = argstr(c, av[0], NULL), *v = argstr(c, av[1], NULL);
    for (char *p = k; *p; p++) *p = tolower((unsigned char)*p);
    set_attr(jsof(c)->d, n, k, v); jsof(c)->d->dom_gen++;
    free(k); free(v); return JS_UNDEFINED;
}
static JSValue n_rmattr(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *k = argstr(c, av[0], NULL);
    rm_attr(n, k); jsof(c)->d->dom_gen++; free(k); return JS_UNDEFINED;
}
static JSValue n_attrnames(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); JSValue a = JS_NewArray(c);
    for (int i = 0; i < n->nattrs; i++) JS_SetPropertyUint32(c, a, i, JS_NewString(c, n->attrs[i].name));
    return a;
}
static JSValue n_parent(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { GETNODE(c, t); return wrap(jsof(c), n->parent); }
static JSValue n_first(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { GETNODE(c, t); return wrap(jsof(c), n->first); }
static JSValue n_last(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { GETNODE(c, t); return wrap(jsof(c), n->last); }
static JSValue n_next(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { GETNODE(c, t); return wrap(jsof(c), n->next); }
static JSValue n_prev(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); Node *p = NULL;
    if (n->parent) for (Node *q = n->parent->first; q && q != n; q = q->next) p = q;
    return wrap(jsof(c), p);
}
static JSValue n_append(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); Node *ch = argnode(c, av[0]);
    if (!ch) return JS_ThrowTypeError(c, "el argumento no es un Node");
    if (is_ancestor(ch, n)) return JS_ThrowTypeError(c, "HierarchyRequestError");
    detach(ch);
    ch->parent = n;
    if (n->last) n->last->next = ch; else n->first = ch;
    n->last = ch;
    jsof(c)->d->dom_gen++;
    return JS_UNDEFINED;
}
static JSValue n_insert(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); Node *ch = argnode(c, av[0]), *ref = ac > 1 ? argnode(c, av[1]) : NULL;
    if (!ch) return JS_ThrowTypeError(c, "el argumento no es un Node");
    if (is_ancestor(ch, n)) return JS_ThrowTypeError(c, "HierarchyRequestError");
    if (ref && ref->parent != n) return JS_ThrowTypeError(c, "NotFoundError");
    if (ref == ch) return JS_UNDEFINED;
    detach(ch);
    ch->parent = n;
    if (!ref) { if (n->last) n->last->next = ch; else n->first = ch; n->last = ch; }
    else if (n->first == ref) { ch->next = ref; n->first = ch; }
    else { Node *q = n->first; while (q->next != ref) q = q->next; q->next = ch; ch->next = ref; }
    jsof(c)->d->dom_gen++;
    return JS_UNDEFINED;
}
static JSValue n_detach(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); detach(n); jsof(c)->d->dom_gen++; return JS_UNDEFINED;
}
static JSValue n_data(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); return n->type == 1 ? JS_NewStringLen(c, n->text, n->tlen) : JS_NewString(c, "");
}
static JSValue n_setdata(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); size_t l; char *s = argstr(c, av[0], &l);
    if (n->type == 1) { n->text = arena_strndup(&jsof(c)->d->arena, s, l); n->tlen = (int)l; jsof(c)->d->dom_gen++; }
    free(s); return JS_UNDEFINED;
}
static JSValue n_html(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *s = node_serialize(n, 0);
    JSValue r = JS_NewString(c, s ? s : ""); free(s); return r;
}
static JSValue n_outer(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *s = node_serialize(n, 1);
    JSValue r = JS_NewString(c, s ? s : ""); free(s); return r;
}
static void clear_children(Node *n) {
    for (Node *q = n->first; q;) { Node *nx = q->next; q->parent = NULL; q->next = NULL; q = nx; }
    n->first = n->last = NULL;
}
static JSValue n_sethtml(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); size_t l; char *s = argstr(c, av[0], &l);
    clear_children(n);
    html_parse_fragment(jsof(c)->d, n, s, l);
    jsof(c)->d->dom_gen++;
    free(s); return JS_UNDEFINED;
}
static JSValue n_text(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t);
    if (n->type == 1) return JS_NewStringLen(c, n->text, n->tlen);
    char *buf = NULL; size_t len = 0, cap = 0;
    text_rec(n, &buf, &len, &cap);
    JSValue r = JS_NewStringLen(c, buf ? buf : "", len); free(buf); return r;
}
static JSValue n_settext(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); size_t l; char *s = argstr(c, av[0], &l); Doc *d = jsof(c)->d;
    if (n->type == 1) { n->text = arena_strndup(&d->arena, s, l); n->tlen = (int)l; }
    else {
        clear_children(n);
        if (l) { Node *tn = mk_node(d, 1, "#text"); tn->text = arena_strndup(&d->arena, s, l); tn->tlen = (int)l; tn->parent = n; n->first = n->last = tn; }
    }
    d->dom_gen++; free(s); return JS_UNDEFINED;
}
static JSValue n_qsa(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *s = argstr(c, av[0], NULL); int first = ac > 1 && JS_ToBool(c, av[1]);
    Node **out = malloc(sizeof(Node *) * 4000);
    int cnt = dom_select(n, s, out, 4000, first);
    JSValue a = JS_NewArray(c);
    for (int i = 0; i < cnt; i++) JS_SetPropertyUint32(c, a, i, wrap(jsof(c), out[i]));
    free(out); free(s); return a;
}
static JSValue n_matches(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); char *s = argstr(c, av[0], NULL); int m = dom_matches(n, s); free(s); return JS_NewBool(c, m);
}
static JSValue n_clone(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    GETNODE(c, t); return wrap(jsof(c), clone_node(jsof(c)->d, n, ac > 0 && JS_ToBool(c, av[0])));
}

/* ---------- globales nativas ---------- */
static JSValue g_create(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    char *nm = argstr(c, av[0], NULL);
    for (char *p = nm; *p; p++) *p = tolower((unsigned char)*p);
    Node *n = mk_node(jsof(c)->d, 0, nm); free(nm);
    return wrap(jsof(c), n);
}
static JSValue g_createtext(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    size_t l; char *s = argstr(c, av[0], &l); Doc *d = jsof(c)->d;
    Node *n = mk_node(d, 1, "#text"); n->text = arena_strndup(&d->arena, s, l); n->tlen = (int)l; free(s);
    return wrap(jsof(c), n);
}
static JSValue g_root(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { return wrap(jsof(c), jsof(c)->d->root); }
static JSValue g_log(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    JS *j = jsof(c);
    if (j->debug) { char *s = argstr(c, av[1], NULL); char *l = argstr(c, av[0], NULL); fprintf(stderr, "[js:%s] %s\n", l, s); free(s); free(l); }
    return JS_UNDEFINED;
}
static JSValue g_now(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { return JS_NewFloat64(c, mono_ms()); }
static JSValue g_pump(JSContext *c, JSValueConst t, int ac, JSValueConst *av) { pump(jsof(c)); return JS_UNDEFINED; }
static JSValue g_resolve(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    char *base = argstr(c, av[0], NULL), *rel = argstr(c, av[1], NULL);
    char out[4096]; url_resolve(base, rel, out, sizeof out);
    free(base); free(rel); return JS_NewString(c, out);
}
static JSValue g_navigate(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    JS *j = jsof(c); char *u = argstr(c, av[0], NULL);
    if (!strncmp(u, "javascript:", 11)) { free(u); return JS_UNDEFINED; }
    char out[4096]; url_resolve(j->d->base, u, out, sizeof out);
    free(j->nav); j->nav = strdup(out); free(u);
    return JS_UNDEFINED;
}

static int allowed_url(JS *j, const char *u) {
    if (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8)) return 1;
    if (j->local && !strncmp(u, "file://", 7)) return 1;
    return 0;
}

static const char *const BLOCKED_HOSTS[] = {
    "google-analytics.com", "googletagmanager.com", "doubleclick.net", "googlesyndication.com", "googleadservices.com",
    "facebook.net", "connect.facebook", "hotjar.com", "segment.com", "segment.io", "mixpanel.com", "amplitude.com",
    "newrelic.com", "nr-data.net", "sentry.io", "clarity.ms", "scorecardresearch.com", "adsystem.com", "adnxs.com",
    "taboola.com", "outbrain.com", "criteo.com", "quantserve.com", "optimizely.com", "fullstory.com", 0 };
static int blocked_host(const char *u) {
    const char *s = strstr(u, "://"); if (!s) return 0;
    s += 3;
    size_t hl = strcspn(s, "/?#:");
    char host[256]; if (hl > 255) hl = 255; memcpy(host, s, hl); host[hl] = 0;
    for (int i = 0; BLOCKED_HOSTS[i]; i++) if (strcasestr(host, BLOCKED_HOSTS[i])) return 1;
    return 0;
}

/* fetch síncrono: (método, url, cuerpo) -> {status, ok, url, ctype, body} */
static JSValue g_fetch(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    JS *j = jsof(c);
    char *method = argstr(c, av[0], NULL), *url = argstr(c, av[1], NULL);
    char abs[4096]; url_resolve(j->d->base, url, abs, sizeof abs);
    JSValue o = JS_NewObject(c);
    int status = 0; const char *body = ""; size_t blen = 0; Resp r; int have = 0;
    char ctype[128] = "", fin[2048] = "";
    if (!strcasecmp(method, "GET") || !strcasecmp(method, "HEAD")) {
        if (allowed_url(j, abs) && !blocked_host(abs) && j->nfetch < 60) {
            j->nfetch++;
            double t0 = mono_ms();
            if (fetch_url(abs, &r, 4u << 20, 10) == 0) { have = 1; status = r.status ? r.status : 200; body = (const char *)r.data; blen = r.len; snprintf(ctype, sizeof ctype, "%s", r.ctype); snprintf(fin, sizeof fin, "%s", r.final_url); }
            if (j->deadline > 0) j->deadline += mono_ms() - t0;   /* la espera de red no cuenta contra el límite de CPU */
        }
    } else status = 204;
    if (!have && status == 0) status = 0;
    JS_SetPropertyStr(c, o, "status", JS_NewInt32(c, status));
    JS_SetPropertyStr(c, o, "ok", JS_NewBool(c, status >= 200 && status < 300));
    JS_SetPropertyStr(c, o, "url", JS_NewString(c, fin[0] ? fin : abs));
    JS_SetPropertyStr(c, o, "ctype", JS_NewString(c, ctype));
    JS_SetPropertyStr(c, o, "body", JS_NewStringLen(c, body ? body : "", blen));
    if (have) resp_free(&r);
    free(method); free(url);
    return o;
}

static void eval_code(JS *j, const char *code, size_t len, const char *fname) {
    char *copy = malloc(len + 1); memcpy(copy, code, len); copy[len] = 0;
    JSValue r = JS_Eval(j->ctx, copy, len, fname, JS_EVAL_TYPE_GLOBAL);
    free(copy);
    if (JS_IsException(r)) js_log_exc(j, fname);
    JS_FreeValue(j->ctx, r);
    pump(j);
}

static int script_type_ok(Node *n) {
    const char *ty = node_attr(n, "type");
    if (node_attr(n, "nomodule") && 0) return 0;
    if (!ty || !*ty) return 1;
    return !strcasecmp(ty, "text/javascript") || !strcasecmp(ty, "application/javascript") || !strcasecmp(ty, "text/ecmascript") ||
           !strcasecmp(ty, "application/ecmascript") || !strcasecmp(ty, "text/babel") * 0 + 0;
}

static ScriptJob *find_cached(JS *j, Node *n) {
    for (int i = 0; i < j->ncache; i++) if (j->cache[i].n == n) return &j->cache[i];
    return NULL;
}

/* ejecuta un <script>: devuelve true si se ejecutó */
static JSValue g_runscript(JSContext *c, JSValueConst t, int ac, JSValueConst *av) {
    JS *j = jsof(c);
    Node *n = argnode(c, av[0]);
    if (!n || n->type != 0 || strcmp(n->name, "script")) return JS_FALSE;
    if (!script_type_ok(n)) return JS_FALSE;
    const char *src = node_attr(n, "src");
    JSValue cs = JS_GetPropertyStr(c, j->docv, "currentScript");
    JS_SetPropertyStr(c, j->docv, "currentScript", wrap(j, n));
    if (src && *src) {
        char abs[4096]; url_resolve(j->d->base, src, abs, sizeof abs);
        ScriptJob *sj = find_cached(j, n);
        if (sj && sj->body) eval_code(j, sj->body, sj->len, abs);
        else if (!sj && allowed_url(j, abs) && !blocked_host(abs) && j->nfetch < 60) {
            Resp r; j->nfetch++;
            double t0 = mono_ms();
            if (fetch_url(abs, &r, 4u << 20, 10) == 0 && r.status < 400 && r.len) eval_code(j, (const char *)r.data, r.len, abs);
            resp_free(&r);
            if (j->deadline > 0) j->deadline += mono_ms() - t0;
        }
    } else {
        char *buf = NULL; size_t len = 0, cap = 0;
        for (Node *ch = n->first; ch; ch = ch->next) if (ch->type == 1) {
            if (len + ch->tlen + 1 > cap) { cap = (len + ch->tlen + 1) * 2; buf = realloc(buf, cap); }
            memcpy(buf + len, ch->text, ch->tlen); len += ch->tlen;
        }
        if (buf && len) eval_code(j, buf, len, "<script>");
        free(buf);
    }
    JS_SetPropertyStr(c, j->docv, "currentScript", cs);
    return JS_TRUE;
}

/* ---------- ciclo de vida ---------- */
static void collect_scripts(Node *n, Node **out, int *cnt, int max) {
    for (Node *c = n->first; c && *cnt < max; c = c->next) {
        if (c->type != 0) continue;
        if (!strcmp(c->name, "script")) { out[(*cnt)++] = c; continue; }
        collect_scripts(c, out, cnt, max);
    }
}

static void *script_thread(void *a) {
    ScriptJob *s = a; Resp r;
    if (fetch_url(s->url, &r, 4u << 20, 10) == 0 && r.status < 400 && r.len > 0) {
        s->body = malloc(r.len + 1); memcpy(s->body, r.data, r.len); s->body[r.len] = 0; s->len = r.len;
    }
    resp_free(&r);
    return NULL;
}

void doc_js_free(Doc *d) {
    JS *j = d->js;
    if (!j) return;
    for (int i = 0; i < j->nwr; i++) { JS_FreeValue(j->ctx, *j->wr[i]); free(j->wr[i]); }
    for (int i = 0; i < j->ncache; i++) free(j->cache[i].body);
    free(j->cache); free(j->wr); free(j->nav);
    JS_FreeValue(j->ctx, j->docv);
    JS_FreeContext(j->ctx);
    JS_FreeRuntime(j->rt);
    free(j);
    d->js = NULL;
}

#define DEF(obj, name, fn, n) JS_SetPropertyStr(c, obj, name, JS_NewCFunction(c, fn, name, n))

static void collect_dyn_styles(Doc *d, Node *n) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type != 0) continue;
        if (!strcmp(c->name, "style")) {
            if (node_attr(c, "data-cx")) continue;
            int any = 0;
            for (Node *t = c->first; t; t = t->next) if (t->type == 1 && t->tlen) { doc_append_css(d, t->text, t->tlen); any = 1; }
            if (any) set_attr(d, c, "data-cx", "1");
        } else collect_dyn_styles(d, c);
    }
}

void js_run_page(Doc *d, int local, int vw, int vh, const char *pageurl) {
    JS *j = calloc(1, sizeof *j);
    j->d = d; j->local = local; j->debug = getenv("COSMOS_JSDEBUG") != NULL;
    snprintf(j->pageurl, sizeof j->pageurl, "%s", pageurl);
    j->rt = JS_NewRuntime();
    JS_SetMemoryLimit(j->rt, 384u << 20);
    JS_SetMaxStackSize(j->rt, 1u << 20);
    JS_SetInterruptHandler(j->rt, interrupt_cb, j);
    JSContext *c = j->ctx = JS_NewContext(j->rt);
    JS_SetContextOpaque(c, j);
    JS_NewClassID(j->rt, &j->cls);
    JSClassDef cd = { "Node", NULL, NULL, NULL, NULL };
    JS_NewClass(j->rt, j->cls, &cd);
    JSValue proto = JS_NewObject(c);
    DEF(proto, "_type", n_type, 0); DEF(proto, "_name", n_name, 0);
    DEF(proto, "_attr", n_attr, 1); DEF(proto, "_setAttr", n_setattr, 2); DEF(proto, "_rmAttr", n_rmattr, 1); DEF(proto, "_attrNames", n_attrnames, 0);
    DEF(proto, "_parent", n_parent, 0); DEF(proto, "_first", n_first, 0); DEF(proto, "_last", n_last, 0);
    DEF(proto, "_next", n_next, 0); DEF(proto, "_prev", n_prev, 0);
    DEF(proto, "_append", n_append, 1); DEF(proto, "_insert", n_insert, 2); DEF(proto, "_detach", n_detach, 0);
    DEF(proto, "_data", n_data, 0); DEF(proto, "_setData", n_setdata, 1);
    DEF(proto, "_html", n_html, 0); DEF(proto, "_outer", n_outer, 0); DEF(proto, "_setHtml", n_sethtml, 1);
    DEF(proto, "_text", n_text, 0); DEF(proto, "_setText", n_settext, 1);
    DEF(proto, "_qsa", n_qsa, 2); DEF(proto, "_matches", n_matches, 1); DEF(proto, "_clone", n_clone, 1);
    JS_SetClassProto(c, j->cls, JS_DupValue(c, proto));
    JSValue glob = JS_GetGlobalObject(c);
    JSValue nodeobj = JS_NewObject(c);
    JS_SetPropertyStr(c, nodeobj, "prototype", proto);
    JS_SetPropertyStr(c, glob, "__Node", nodeobj);
    DEF(glob, "__create", g_create, 1); DEF(glob, "__createText", g_createtext, 1); DEF(glob, "__root", g_root, 0);
    DEF(glob, "__log", g_log, 2); DEF(glob, "__now", g_now, 0); DEF(glob, "__pump", g_pump, 0);
    DEF(glob, "__resolve", g_resolve, 2); DEF(glob, "__navigate", g_navigate, 1);
    DEF(glob, "__fetch", g_fetch, 3); DEF(glob, "__runScript", g_runscript, 1);
    JS_SetPropertyStr(c, glob, "__pageurl", JS_NewString(c, pageurl));
    JS_SetPropertyStr(c, glob, "__vw", JS_NewInt32(c, vw));
    JS_SetPropertyStr(c, glob, "__vh", JS_NewInt32(c, vh));
    JS_FreeValue(c, glob);
    d->js = j;

    j->deadline = mono_ms() + 8000;
    eval_code(j, PRELUDE_JS, strlen(PRELUDE_JS), "prelude.js");
    glob = JS_GetGlobalObject(c);
    j->docv = JS_GetPropertyStr(c, glob, "document");
    JS_FreeValue(c, glob);
    if (JS_IsUndefined(j->docv) || JS_IsException(j->docv)) { j->docv = JS_UNDEFINED; return; }

    /* precarga en paralelo de scripts externos */
    Node *scripts[200]; int ns = 0;
    collect_scripts(d->root, scripts, &ns, 200);
    ScriptJob *jobs = calloc(16, sizeof *jobs); int nj = 0;
    for (int i = 0; i < ns && nj < 16; i++) {
        const char *src = node_attr(scripts[i], "src");
        if (!src || !*src || !script_type_ok(scripts[i])) continue;
        char abs[4096]; url_resolve(d->base, src, abs, sizeof abs);
        jobs[nj].n = scripts[i];
        snprintf(jobs[nj].url, sizeof jobs[nj].url, "%s", abs);
        if (allowed_url(j, abs) && !blocked_host(abs)) nj++;
    }
    pthread_t th[16];
    int started[16] = {0};
    for (int i = 0; i < nj; i++) started[i] = pthread_create(&th[i], NULL, script_thread, &jobs[i]) == 0;
    for (int i = 0; i < nj; i++) if (started[i]) pthread_join(th[i], NULL);
    j->cache = jobs; j->ncache = nj;
    j->nfetch += nj;

    j->deadline = mono_ms() + 6000;
    for (int i = 0; i < ns; i++) {
        JSValue f = JS_NewCFunction(c, g_runscript, "x", 1);
        JSValue arg = wrap(j, scripts[i]);
        JSValue r = JS_Call(c, f, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(c, r); JS_FreeValue(c, arg); JS_FreeValue(c, f);
        if (j->deadline > 0 && mono_ms() > j->deadline) { if (j->debug) fprintf(stderr, "[js] presupuesto de CPU agotado\n"); break; }
    }
    j->deadline = mono_ms() + 3000;
    JSValue g2 = JS_GetGlobalObject(c);
    JSValue fn = JS_GetPropertyStr(c, g2, "__loaded");
    if (JS_IsFunction(c, fn)) { JSValue r = JS_Call(c, fn, JS_UNDEFINED, 0, NULL); if (JS_IsException(r)) js_log_exc(j, "__loaded"); JS_FreeValue(c, r); }
    JS_FreeValue(c, fn); JS_FreeValue(c, g2);
    pump(j);
    j->deadline = 0;
    if (getenv("COSMOS_JSEVAL")) {
        const char *ex = getenv("COSMOS_JSEVAL");
        JSValue r = JS_Eval(c, ex, strlen(ex), "<eval>", JS_EVAL_TYPE_GLOBAL);
        const char *s = JS_ToCString(c, JS_IsException(r) ? JS_GetException(c) : r);
        fprintf(stderr, "[eval] %s\n", s ? s : "?");
        if (s) JS_FreeCString(c, s);
        JS_FreeValue(c, r);
    }
    collect_dyn_styles(d, d->root);
}

/* llamadas desde la interfaz (hilo de la UI; el documento ya no lo toca el hilo de carga) */
static int call_global(JS *j, const char *name, int argc, JSValue *argv, int budget_ms) {
    JSContext *c = j->ctx;
    JS_UpdateStackTop(j->rt);
    JSValue g = JS_GetGlobalObject(c);
    JSValue fn = JS_GetPropertyStr(c, g, name);
    int ret = 0;
    if (JS_IsFunction(c, fn)) {
        j->deadline = mono_ms() + budget_ms;
        JSValue r = JS_Call(c, fn, JS_UNDEFINED, argc, argv);
        if (JS_IsException(r)) js_log_exc(j, name);
        else if (JS_IsBool(r)) ret = JS_ToBool(c, r);
        JS_FreeValue(c, r);
        pump(j);
        j->deadline = 0;
    }
    JS_FreeValue(c, fn); JS_FreeValue(c, g);
    return ret;
}

/* despacha un evento (click, input...) sobre un nodo; devuelve 1 si algún manejador llamó preventDefault */
int js_dispatch(Doc *d, Node *target, const char *type) {
    JS *j = d->js;
    if (!j || JS_IsUndefined(j->docv)) return 0;
    JSValue argv[2] = { wrap(j, target), JS_NewString(j->ctx, type) };
    int r = call_global(j, "__dispatchNative", 2, argv, 300);
    JS_FreeValue(j->ctx, argv[0]); JS_FreeValue(j->ctx, argv[1]);
    return r;
}

/* avanza el reloj virtual de temporizadores; devuelve 1 si el DOM cambió */
int js_tick(Doc *d, int ms) {
    JS *j = d->js;
    if (!j || JS_IsUndefined(j->docv)) return 0;
    int before = d->dom_gen;
    JSValue a = JS_NewInt32(j->ctx, ms);
    call_global(j, "__advance", 1, &a, 120);
    return d->dom_gen != before;
}

int js_has_timers(Doc *d) {
    JS *j = d->js;
    if (!j || JS_IsUndefined(j->docv)) return 0;
    return call_global(j, "__hasTimers", 0, NULL, 50);
}

char *js_take_nav(Doc *d) {
    JS *j = d->js;
    if (!j || !j->nav) return NULL;
    char *r = j->nav; j->nav = NULL; return r;
}

void js_sync_styles(Doc *d) { collect_dyn_styles(d, d->root); }
