/* Cosmos Browser - interfaz, pestañas, hilos de carga y bucle principal */
#include "cosmos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <libgen.h>
#include <limits.h>
#include <strings.h>
#ifndef HEADLESS
#include <SDL2/SDL.h>
#endif

#define TABH 32
#define TOOLH 40
#define STATH 22
#define TOP (TABH + TOOLH)
#define SBW 10

#define C_BG       0x0b0d1e
#define C_TABBG    0x080a17
#define C_TAB      0x151a3f
#define C_TABACT   0x262c6e
#define C_ACCENT   0x5de0ff
#define C_PURPLE   0x9b6bff
#define C_TEXT     0xe6e8ff
#define C_DIM      0x8d95c9
#define C_URL      0x12163a
#define C_URLFOCUS 0x1c2352
#define C_STATUS   0x0a0c1c
#define UIPX 13

/* ================= estado ================= */
static pthread_mutex_t G = PTHREAD_MUTEX_INITIALIZER;
#define LOCK pthread_mutex_lock(&G)
#define UNLOCK pthread_mutex_unlock(&G)

typedef struct Tab {
    char url[2048], status[160], frag[128];
    char **hist; int *hsc; int nh, hcap, hi;
    Doc *doc, *pdoc;
    Page pg;
    int scroll, restore_scroll, need_layout, reader;
    int gen, loading, ready, dirty, closed, refs, code;
    char purl[2048], perr[160];
    Player *pl;
    int seen_gen;
} Tab;

typedef struct { Tab *t; int gen; char url[2048]; } Job;

static Tab *tabs[32];
static int fullvid;
static long audio_bytes;
static int pl_rx, pl_ry, pl_rw, pl_rh;   /* región del reproductor dibujada (para clics) */
#define CTRLH 58
static int ntabs, cur;
static float zoom = 1.0f;
static Fb fb;
static int W = 1100, H = 700;
static char ubuf[2048];
static int ucur, usel, ufocus;
static char hover_url[2048];
static char exe_dir[PATH_MAX];

/* ================= utilidades ================= */
typedef struct { char *s; size_t n, cap; } Buf;
static void buf_add(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2; b->s = realloc(b->s, b->cap); }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
static void buf_esc(Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '<') buf_str(b, "&lt;");
        else if (s[i] == '>') buf_str(b, "&gt;");
        else if (s[i] == '&') buf_str(b, "&amp;");
        else if (s[i] == '"') buf_str(b, "&quot;");
        else buf_add(b, s + i, 1);
    }
}

static int utf8_valid(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        int k = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (k < 0) return 0;
        if (k > 0 && i + k >= n) return 0;
        for (int j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}
static char *latin1_to_utf8(const uint8_t *s, size_t n, size_t *outn) {
    char *o = malloc(n * 2 + 1), *w = o;
    for (size_t i = 0; i < n; i++) w += utf8_put(w, s[i]);
    *w = 0;
    *outn = (size_t)(w - o);
    return o;
}

static const char *scheme_of(const char *u) {
    static char sc[16];
    int k = 0;
    while (u[k] && u[k] != ':' && k < 15) { sc[k] = tolower((unsigned char)u[k]); k++; }
    sc[k] = 0;
    return sc;
}

/* ================= carga (hilo) ================= */
static void doc_unref_locked(Doc *d) { if (d && --d->refs == 0) doc_free(d); }

static void tab_unref_locked(Tab *t) {
    if (--t->refs > 0) return;
    for (int i = 0; i < t->nh; i++) free(t->hist[i]);
    free(t->hist); free(t->hsc);
    if (t->pl) { player_close(t->pl); t->pl = NULL; }
    page_clear(&t->pg);
    free(t->pg.it); free(t->pg.an);
    doc_unref_locked(t->doc);
    doc_unref_locked(t->pdoc);
    free(t);
}

/* ---------- hojas de estilo externas ---------- */
typedef struct { char url[2048]; Resp r; int rc; } SheetJob;
static void *sheet_thread(void *a) { SheetJob *j = a; j->rc = fetch_url(j->url, &j->r, 2u << 20, 10); return NULL; }

static void find_links(Node *n, Doc *d, SheetJob *jobs, int *cnt, int max) {
    for (Node *c = n->first; c && *cnt < max; c = c->next) {
        if (c->type != 0) continue;
        if (!strcmp(c->name, "link")) {
            const char *rel = node_attr(c, "rel"), *href = node_attr(c, "href"), *media = node_attr(c, "media");
            if (rel && href && *href && strcasestr(rel, "stylesheet") && !strcasestr(rel, "alternate") &&
                !(media && strcasestr(media, "print") && !strcasestr(media, "screen"))) {
                url_resolve(d->base, href, jobs[*cnt].url, sizeof jobs[*cnt].url);
                if (!strncmp(jobs[*cnt].url, "http", 4) || !strncmp(jobs[*cnt].url, "file:", 5)) (*cnt)++;
            }
        }
        find_links(c, d, jobs, cnt, max);
    }
}

static void run_sheets(SheetJob *jobs, int n) {
    pthread_t th[12];
    for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, sheet_thread, &jobs[i]);
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
}

static int sheet_ok(SheetJob *j) { return j->rc == 0 && j->r.len > 0 && j->r.status < 400 && !strstr(j->r.ctype, "html"); }

static void fetch_stylesheets(Doc *d) {
    SheetJob *jobs = calloc(8, sizeof *jobs);
    int n = 0;
    find_links(d->root, d, jobs, &n, 8);
    if (!n) { free(jobs); return; }
    run_sheets(jobs, n);
    /* @import de un nivel */
    SheetJob *imp = calloc(8, sizeof *imp);
    int owner[8], ni = 0;
    for (int i = 0; i < n && ni < 8; i++) {
        if (!sheet_ok(&jobs[i])) continue;
        const char *css = (const char *)jobs[i].r.data;
        size_t len = jobs[i].r.len;
        for (size_t p = 0; p + 8 < len && p < 4000 && ni < 8; p++) {
            if (strncasecmp(css + p, "@import", 7)) continue;
            const char *q = css + p + 7;
            while (q < css + len && (isspace((unsigned char)*q) || !strncasecmp(q, "url(", 4))) q += (*q == 'u' || *q == 'U') ? 4 : 1;
            if (q < css + len && (*q == '"' || *q == '\'')) {
                char qc = *q++; const char *e = q;
                while (e < css + len && *e != qc && e - q < 1000) e++;
                char rel[1024]; size_t rl = e - q; if (rl > 1023) rl = 1023;
                memcpy(rel, q, rl); rel[rl] = 0;
                url_resolve(jobs[i].url, rel, imp[ni].url, sizeof imp[ni].url);
                if (!strncmp(imp[ni].url, "http", 4) || !strncmp(imp[ni].url, "file:", 5)) owner[ni++] = i;
            } else if (q < css + len) {
                const char *e = q; while (e < css + len && *e != ')' && *e != ';' && !isspace((unsigned char)*e) && e - q < 1000) e++;
                char rel[1024]; size_t rl = e - q; if (rl > 1023) rl = 1023;
                memcpy(rel, q, rl); rel[rl] = 0;
                url_resolve(jobs[i].url, rel, imp[ni].url, sizeof imp[ni].url);
                if (!strncmp(imp[ni].url, "http", 4) || !strncmp(imp[ni].url, "file:", 5)) owner[ni++] = i;
            }
        }
    }
    if (ni) run_sheets(imp, ni);
    Buf all = {0};
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < ni; k++) if (owner[k] == i && sheet_ok(&imp[k])) { buf_add(&all, (const char *)imp[k].r.data, imp[k].r.len); buf_str(&all, "\n"); }
        if (sheet_ok(&jobs[i])) { buf_add(&all, (const char *)jobs[i].r.data, jobs[i].r.len); buf_str(&all, "\n"); }
    }
    for (int i = 0; i < n; i++) resp_free(&jobs[i].r);
    for (int i = 0; i < ni; i++) resp_free(&imp[i].r);
    if (all.n) doc_prepend_css(d, all.s, all.n);
    free(all.s); free(jobs); free(imp);
}

static Doc *build_doc(Resp *r, int rc, const char *req_url, const char *final_url) {
    Buf b = {0};
    const char *html = NULL; size_t hlen = 0;
    char *owned = NULL;
    if (rc != 0) {
        buf_str(&b, "<html><head><title>No se pudo abrir la p\xc3\xa1gina</title></head>"
                    "<body style=\"background-color:#0b0d1e;color:#dfe3ff;margin:32px\">"
                    "<h1 style=\"color:#ff7a90\">No se pudo abrir la p\xc3\xa1gina</h1><p>");
        buf_esc(&b, req_url, strlen(req_url));
        buf_str(&b, "</p><p style=\"color:#ffd98a\">");
        buf_esc(&b, r->err[0] ? r->err : "Error desconocido", strlen(r->err[0] ? r->err : "Error desconocido"));
        buf_str(&b, "</p><p><a style=\"color:#7fd3ff\" href=\"");
        buf_esc(&b, req_url, strlen(req_url));
        buf_str(&b, "\">Reintentar</a> &middot; <a style=\"color:#7fd3ff\" href=\"cosmos:home\">Inicio</a></p></body></html>");
        html = b.s; hlen = b.n;
    } else if (strstr(r->ctype, "html") || strstr(r->ctype, "xhtml") ||
               (!r->ctype[0] && r->len > 0 && (r->data[0] == '<'))) {
        if (!utf8_valid(r->data, r->len)) { size_t n2; owned = latin1_to_utf8(r->data, r->len, &n2); html = owned; hlen = n2; }
        else { html = (const char *)r->data; hlen = r->len; }
    } else if (!strncmp(r->ctype, "image/", 6)) {
        buf_str(&b, "<html><body style=\"background-color:#111;margin:0\"><p align=\"center\"><img src=\"");
        buf_esc(&b, final_url, strlen(final_url));
        buf_str(&b, "\"></p></body></html>");
        html = b.s; hlen = b.n;
    } else if (!strncmp(r->ctype, "text/", 5) || strstr(r->ctype, "json") || strstr(r->ctype, "xml") || strstr(r->ctype, "javascript")) {
        size_t n = r->len > (2u << 20) ? (2u << 20) : r->len;
        buf_str(&b, "<html><body><pre>");
        if (!utf8_valid(r->data, n)) { size_t n2; owned = latin1_to_utf8(r->data, n, &n2); buf_esc(&b, owned, n2); }
        else buf_esc(&b, (const char *)r->data, n);
        buf_str(&b, "</pre></body></html>");
        html = b.s; hlen = b.n;
    } else {
        buf_str(&b, "<html><body style=\"background-color:#0b0d1e;color:#dfe3ff;margin:32px\"><h1 style=\"color:#ffd98a\">Tipo de archivo no soportado</h1><p>");
        buf_esc(&b, r->ctype[0] ? r->ctype : "desconocido", strlen(r->ctype[0] ? r->ctype : "desconocido"));
        buf_str(&b, "</p><p>Cosmos Browser todav\xc3\xad""a no descarga archivos.</p><p><a style=\"color:#7fd3ff\" href=\"cosmos:home\">Inicio</a></p></body></html>");
        html = b.s; hlen = b.n;
    }
    Doc *d = html_parse(html, hlen, rc ? req_url : final_url);
    free(b.s); free(owned);
    if (rc == 0) fetch_stylesheets(d);
    if (rc == 0 && !getenv("COSMOS_NOJS") && strncmp(final_url, "cosmos:", 7)) {
        js_run_page(d, !strncmp(final_url, "file://", 7), css_viewport_w, css_viewport_h, final_url);
    }
    doc_style(d);
    doc_collect_images(d);
    return d;
}

static Image *decode_data_url(const char *u) {
    const char *comma = strchr(u, ',');
    if (!comma) return NULL;
    if (!strstr(u, ";base64")) return NULL;
    size_t n = strlen(comma + 1);
    uint8_t *out = malloc(n);
    size_t o = 0;
    int acc = 0, bits = 0;
    for (const char *p = comma + 1; *p; p++) {
        int v;
        if (*p >= 'A' && *p <= 'Z') v = *p - 'A';
        else if (*p >= 'a' && *p <= 'z') v = *p - 'a' + 26;
        else if (*p >= '0' && *p <= '9') v = *p - '0' + 52;
        else if (*p == '+' || *p == '-') v = 62;
        else if (*p == '/' || *p == '_') v = 63;
        else continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (acc >> bits) & 255; }
    }
    Image *im = image_decode(out, o);
    free(out);
    return im;
}

static void load_images(Tab *t, Doc *doc, int gen, int from) {
    for (int k = from; k < doc->nimgs; k++) {
        LOCK;
        int ok = (t->gen == gen && !t->closed);
        if (ok) snprintf(t->status, sizeof t->status, "Cargando im\xc3\xa1genes %d/%d...", k + 1, doc->nimgs);
        UNLOCK;
        if (!ok) break;
        Node *n = doc->imgs[k];
        Image *im = NULL;
        if (!strncmp(n->img_url, "data:", 5)) im = decode_data_url(n->img_url);
        else {
            Resp ir;
            if (fetch_url(n->img_url, &ir, 8u << 20, 12) == 0 && ir.len > 0) im = image_decode(ir.data, ir.len);
            resp_free(&ir);
        }
        LOCK;
        if (t->gen == gen && !t->closed) { n->img = im; n->img_state = im ? 2 : 3; t->dirty = 1; }
        else image_free(im);
        UNLOCK;
    }
}

static void *worker(void *arg) {
    Job *j = arg;
    Tab *t = j->t;
    int gen = j->gen;
    Resp r;
    int rc = fetch_url(j->url, &r, 24u << 20, 25);
    const char *final = r.final_url[0] ? r.final_url : j->url;
    Doc *doc = build_doc(&r, rc, j->url, final);
    int code = rc ? 0 : r.status;
    char finalbuf[2048];
    snprintf(finalbuf, sizeof finalbuf, "%s", final);
    resp_free(&r);
    LOCK;
    if (t->gen == gen && !t->closed) {
        if (t->pdoc) doc_unref_locked(t->pdoc);
        t->pdoc = doc; doc->refs++;
        snprintf(t->purl, sizeof t->purl, "%s", finalbuf);
        t->code = code;
        t->ready = 1;
    }
    int current = (t->gen == gen && !t->closed);
    UNLOCK;
    if (current) load_images(t, doc, gen, 0);
    LOCK;
    if (t->gen == gen) { t->loading = 0; t->status[0] = 0; }
    doc_unref_locked(doc);
    tab_unref_locked(t);
    UNLOCK;
    free(j);
    return NULL;
}

/* ================= pestañas ================= */
static Tab *tab_new(void) {
    Tab *t = calloc(1, sizeof *t);
    t->refs = 1; t->hi = -1;
    return t;
}

static void tab_start_load(Tab *t, const char *url) {
    css_viewport_w = W; css_viewport_h = H - TOP - STATH;
    Job *j = malloc(sizeof *j);
    j->t = t;
    snprintf(j->url, sizeof j->url, "%s", url);
    LOCK;
    t->gen++;
    j->gen = t->gen;
    t->loading = 1; t->ready = 0; t->refs++;
    snprintf(t->status, sizeof t->status, "Conectando...");
    UNLOCK;
    pthread_t th;
    pthread_create(&th, NULL, worker, j);
    pthread_detach(th);
}

static void sync_urlbar(void) {
    if (ufocus || !ntabs) return;
    snprintf(ubuf, sizeof ubuf, "%s", tabs[cur]->url);
    ucur = (int)strlen(ubuf);
}

typedef struct { Tab *t; Doc *doc; int gen, from; } ImgJob;
static void *img_thread(void *a) {
    ImgJob *j = a;
    load_images(j->t, j->doc, j->gen, j->from);
    LOCK;
    doc_unref_locked(j->doc); tab_unref_locked(j->t);
    UNLOCK;
    free(j);
    return NULL;
}

/* si el JS cambió el DOM: recalcula estilos, recoge imágenes nuevas y pide relayout */
static void tab_js_sync(Tab *t) {
    Doc *d = t->doc;
    if (!d || !d->js || d->dom_gen == t->seen_gen) return;
    t->seen_gen = d->dom_gen;
    int before = d->nimgs;
    LOCK;
    js_sync_styles(d);
    doc_style(d);
    doc_collect_images(d);
    if (d->nimgs > before) {
        ImgJob *j = malloc(sizeof *j);
        j->t = t; j->doc = d; j->gen = t->gen; j->from = before;
        d->refs++; t->refs++;
        pthread_t th;
        if (pthread_create(&th, NULL, img_thread, j) == 0) pthread_detach(th);
        else { d->refs--; t->refs--; free(j); }
    }
    UNLOCK;
    t->need_layout = 1;
}

static void tab_layout(Tab *t) {
    if (!t->doc) return;
    LOCK;
    layout_doc(t->doc, &t->pg, W - SBW, zoom, t->reader);
    UNLOCK;
    t->need_layout = 0;
    int vh = H - TOP - STATH;
    int maxs = t->pg.height - vh;
    if (maxs < 0) maxs = 0;
    if (t->scroll > maxs) t->scroll = maxs;
    if (t->scroll < 0) t->scroll = 0;
}

static void tab_scroll_to_frag(Tab *t) {
    if (!t->frag[0]) return;
    int y = page_anchor(&t->pg, t->frag);
    if (y >= 0) {
        int vh = H - TOP - STATH, maxs = t->pg.height - vh;
        if (maxs < 0) maxs = 0;
        t->scroll = y > maxs ? maxs : y;
        t->frag[0] = 0;
    }
}

static void tab_navigate(Tab *t, const char *url, int kind) {   /* 0 nueva, 1 recargar, 2 historial */
    char fetchurl[2048];
    snprintf(fetchurl, sizeof fetchurl, "%s", url);
    char *hash = strchr(fetchurl, '#');
    char frag[128] = "";
    if (hash) { snprintf(frag, sizeof frag, "%s", hash + 1); *hash = 0; }
    if (kind == 0 && frag[0] && t->doc) {                      /* ancla en la misma página */
        char cur_nof[2048];
        snprintf(cur_nof, sizeof cur_nof, "%s", t->url);
        char *h2 = strchr(cur_nof, '#'); if (h2) *h2 = 0;
        if (!strcmp(cur_nof, fetchurl)) {
            int y = page_anchor(&t->pg, frag);
            if (y >= 0) {
                if (t->hi >= 0) t->hsc[t->hi] = t->scroll;
                int vh = H - TOP - STATH, maxs = t->pg.height - vh;
                if (maxs < 0) maxs = 0;
                t->scroll = y > maxs ? maxs : y;
                snprintf(t->url, sizeof t->url, "%s", url);
                if (t->nh >= t->hi + 2) {
                    for (int i = t->hi + 1; i < t->nh; i++) free(t->hist[i]);
                    t->nh = t->hi + 1;
                }
                if (t->nh == t->hcap) { t->hcap = t->hcap ? t->hcap * 2 : 16; t->hist = realloc(t->hist, t->hcap * sizeof(char *)); t->hsc = realloc(t->hsc, t->hcap * sizeof(int)); }
                t->hist[t->nh] = strdup(url); t->hsc[t->nh] = 0; t->hi = t->nh++;
                return;
            }
        }
    }
    int direct = 0;
    int isv = is_video_url(fetchurl, &direct);
    if (t->pl) { player_close(t->pl); t->pl = NULL; fullvid = 0; }
    if (kind == 0) {
        t->reader = 0;
        if (t->hi >= 0) t->hsc[t->hi] = t->scroll;
        for (int i = t->hi + 1; i < t->nh; i++) free(t->hist[i]);
        t->nh = t->hi + 1;
        if (t->nh == t->hcap) { t->hcap = t->hcap ? t->hcap * 2 : 16; t->hist = realloc(t->hist, t->hcap * sizeof(char *)); t->hsc = realloc(t->hsc, t->hcap * sizeof(int)); }
        t->hist[t->nh] = strdup(url); t->hsc[t->nh] = 0; t->hi = t->nh++;
        t->restore_scroll = 0;
    } else if (kind == 2) t->restore_scroll = t->hsc[t->hi];
    else t->restore_scroll = t->scroll;
    snprintf(t->url, sizeof t->url, "%s", url);
    snprintf(t->frag, sizeof t->frag, "%s", frag);
    if (isv) {
        LOCK; t->gen++; t->loading = 0; t->status[0] = 0; UNLOCK;
        t->pl = player_open(fetchurl, direct, 0, 0);
        return;
    }
    tab_start_load(t, fetchurl);
}

static int tab_poll(Tab *t) {
    int changed = 0;
    LOCK;
    if (t->ready) {
        Doc *old = t->doc;
        t->doc = t->pdoc; t->pdoc = NULL;
        t->ready = 0;
        t->seen_gen = t->doc ? t->doc->dom_gen : 0;
        char fr[2200];
        if (t->frag[0]) snprintf(fr, sizeof fr, "%s#%s", t->purl, t->frag); else snprintf(fr, sizeof fr, "%s", t->purl);
        snprintf(t->url, sizeof t->url, "%s", fr);
        if (t->hi >= 0 && t->hi < t->nh) { free(t->hist[t->hi]); t->hist[t->hi] = strdup(fr); }
        page_clear(&t->pg);
        doc_unref_locked(old);
        t->scroll = t->restore_scroll;
        t->need_layout = 1;
        changed = 1;
    }
    if (t->dirty) { t->dirty = 0; t->need_layout = 1; changed = 1; }
    UNLOCK;
    return changed;
}

static const char *tab_title(Tab *t) {
    if (t->pl) { static char tt[300]; player_info(t->pl, tt, sizeof tt, NULL, 0); return tt; }
    if (t->doc && t->doc->title && *t->doc->title) return t->doc->title;
    if (!strcmp(t->url, "cosmos:home")) return "Cosmos Browser";
    return t->url[0] ? t->url : "Nueva pesta\xc3\xb1" "a";
}

static void tab_open(const char *url) {
    if (ntabs >= 32) return;
    Tab *t = tab_new();
    tabs[ntabs++] = t;
    cur = ntabs - 1;
    ufocus = 0;
    tab_navigate(t, url, 0);
    sync_urlbar();
}

static void tab_close(int i) {
    Tab *t = tabs[i];
    LOCK;
    t->closed = 1;
    t->gen++;
    if (t->pl) { player_close(t->pl); t->pl = NULL; fullvid = 0; }
    tab_unref_locked(t);
    UNLOCK;
    for (int k = i; k < ntabs - 1; k++) tabs[k] = tabs[k + 1];
    ntabs--;
    if (cur >= ntabs) cur = ntabs - 1;
    if (cur < 0) cur = 0;
    ufocus = 0;
    sync_urlbar();
}

/* ================= dibujo ================= */
static void draw_text_fit(int x, int y, int font, int px, uint32_t col, const char *s, int maxw) {
    int len = (int)strlen(s);
    if (text_w(font, px, s, len) <= maxw) { fb_text(&fb, x, y, font, px, col, s, len); return; }
    const char *ell = "\xe2\x80\xa6";
    int ew = text_w(font, px, ell, 3);
    int k = 0, w = 0;
    const char *p = s, *e = s + len;
    while (p < e) {
        const char *q = p;
        int cp = utf8_next(&q, e);
        char tmp[4]; int tl = utf8_put(tmp, cp);
        int cw = text_w(font, px, tmp, tl);
        if (w + cw + ew > maxw) break;
        w += cw; p = q; k = (int)(p - s);
    }
    int d = fb_text(&fb, x, y, font, px, col, s, k);
    fb_text(&fb, x + d, y, font, px, col, ell, 3);
}

static void tab_geom(int i, int *x, int *w) {
    int avail = W - 44 - 8;
    int tw = ntabs ? avail / ntabs : 200;
    if (tw > 210) tw = 210;
    if (tw < 70) tw = 70;
    *w = tw - 2;
    *x = 6 + i * tw;
}

static void btn_rect(int idx, int *x, int *y, int *w, int *h) {   /* 0 atrás 1 adelante 2 recargar 3 inicio */
    *x = 8 + idx * 34; *y = TABH + 5; *w = 30; *h = 30;
}
static void urlbar_rect(int *x, int *y, int *w, int *h) {
    *x = 8 + 4 * 34 + 4; *y = TABH + 5; *w = W - *x - 10 - 38; *h = 30;
}
static void reader_rect(int *x, int *y, int *w, int *h) { *x = W - 8 - 32; *y = TABH + 5; *w = 32; *h = 30; }

static unsigned tick;

static void draw_stars(int y0, int h) {
    unsigned s = 12345;
    for (int i = 0; i < 70; i++) {
        s = s * 1103515245u + 12345u;
        int x = (s >> 8) % (unsigned)W;
        s = s * 1103515245u + 12345u;
        int y = y0 + (s >> 8) % (unsigned)h;
        uint32_t c = (i % 7 == 0) ? 0x4a4f9a : ((i % 3 == 0) ? 0x2c3070 : 0x1c2050);
        fb_rect(&fb, x, y, 1, 1, c);
    }
}

static void fmt_time(char *o, size_t c, double sec) {
    int t = (int)sec; if (t < 0) t = 0;
    if (t >= 3600) snprintf(o, c, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else snprintf(o, c, "%d:%02d", t / 60, t % 60);
}

static void draw_wrapped(int x, int y, int w, int px, uint32_t col, const char *s) {
    int len = (int)strlen(s), i = 0;
    while (i < len) {
        int end = i, lastsp = -1;
        while (end < len) {
            if (s[end] == ' ') lastsp = end;
            if (text_w(FONT_SANS, px, s + i, end - i + 1) > w) break;
            end++;
        }
        if (end < len && lastsp > i) end = lastsp;
        if (end == i) end = i + 1;
        fb_text(&fb, x, y, FONT_SANS, px, col, s + i, end - i);
        y += px + 6;
        i = end;
        while (i < len && s[i] == ' ') i++;
    }
}

static void draw_player(Player *pl, int x0, int y0, int w, int h) {
    pl_rx = x0; pl_ry = y0; pl_rw = w; pl_rh = h;
    fb_clip(&fb, x0, y0, w, h);
    fb_rect(&fb, x0, y0, w, h, 0x000000);
    int ah = h - CTRLH;
    int st = player_state(pl);
    char title[300], err[512];
    player_info(pl, title, sizeof title, err, sizeof err);
    Image *im = player_lock_frame(pl);
    if (im) {
        int dw = w, dh = w * im->h / im->w;
        if (dh > ah) { dh = ah; dw = ah * im->w / im->h; }
        fb_image(&fb, im, x0 + (w - dw) / 2, y0 + (ah - dh) / 2, dw, dh);
        player_unlock_frame(pl);
    }
    const char *msg = NULL; uint32_t mc = C_ACCENT;
    if (st == PL_RESOLVING) msg = "Buscando el video con yt-dlp...";
    else if (st == PL_ERROR) { msg = err; mc = 0xff7a7a; }
    else if (st == PL_ENDED) msg = "Termin\xc3\xb3. Presiona Espacio para repetir.";
    else if (!im) msg = "Cargando cuadros...";
    if (msg) {
        if (st == PL_ERROR) draw_wrapped(x0 + 30, y0 + 30, w - 60, 15, mc, msg);
        else { int tw = text_w(FONT_SANS, 16, msg, (int)strlen(msg)); fb_text(&fb, x0 + (w - tw) / 2, y0 + ah / 2 - 8, FONT_SANS, 16, mc, msg, (int)strlen(msg)); }
    }
    /* controles */
    int cy = y0 + h - CTRLH;
    fb_rect(&fb, x0, cy, w, CTRLH, 0x0e1130);
    double pos = player_pos(pl), dur = player_dur(pl);
    int bx = x0 + 16, bw = w - 32, by = cy + 12;
    fb_rrect(&fb, bx, by, bw, 8, 4, 0x2a2f66);
    if (dur > 0) {
        int fw = (int)(bw * (pos / dur));
        if (fw > bw) fw = bw;
        if (fw > 0) fb_rrect(&fb, bx, by, fw < 8 ? 8 : fw, 8, 4, C_ACCENT);
        fb_rrect(&fb, bx + fw - 6 < bx ? bx : bx + fw - 6, by - 3, 12, 14, 6, 0xffffff);
    }
    char a[32], b[32], line[200];
    fmt_time(a, sizeof a, pos); fmt_time(b, sizeof b, dur);
    const char *ico = st == PL_PLAYING ? "\xe2\x96\xb6" : "II";
    if (dur > 0) snprintf(line, sizeof line, "%s  %s / %s", ico, a, b); else snprintf(line, sizeof line, "%s  %s", ico, a);
    fb_text(&fb, bx, cy + 30, FONT_SANS, 14, C_TEXT, line, (int)strlen(line));
    int v = player_vol(pl);
    char vb[48];
    const char *hwn = player_hw(pl) ? " VA-API" : "";
    if (v < 0) snprintf(vb, sizeof vb, "%dp%s  Silencio (M)", player_quality(pl), hwn); else snprintf(vb, sizeof vb, "%dp%s  Vol %d%%", player_quality(pl), hwn, v);
    int vw2 = text_w(FONT_SANS, 14, vb, (int)strlen(vb));
    fb_text(&fb, x0 + w - 16 - vw2, cy + 30, FONT_SANS, 14, C_DIM, vb, (int)strlen(vb));
    int lw = text_w(FONT_SANS, 14, line, (int)strlen(line));
    draw_text_fit(bx + lw + 24, cy + 30, FONT_BOLD, 14, C_ACCENT, title, w - 32 - lw - 24 - vw2 - 24);
    fb_noclip(&fb);
}

static void draw_ui(void) {
    Tab *t = tabs[cur];
    int vh = H - TOP - STATH;
    if (fullvid && t->pl) { fb_noclip(&fb); draw_player(t->pl, 0, 0, W, H); return; }
    fb_noclip(&fb);
    fb_rect(&fb, 0, 0, W, TOP, C_BG);
    fb_rect(&fb, 0, 0, W, TABH, C_TABBG);
    draw_stars(0, TOP);
    /* pestañas */
    for (int i = 0; i < ntabs; i++) {
        int x, w; tab_geom(i, &x, &w);
        uint32_t bg = (i == cur) ? C_TABACT : C_TAB;
        fb_rrect(&fb, x, 5, w, TABH - 5, 7, bg);
        fb_rect(&fb, x, TABH - 8, w, 8, bg);
        if (i == cur) fb_rect(&fb, x + 8, 5, w - 16, 2, C_ACCENT);
        draw_text_fit(x + 10, 9, i == cur ? FONT_BOLD : FONT_SANS, UIPX, i == cur ? C_TEXT : C_DIM, tab_title(tabs[i]), w - 36);
        fb_text(&fb, x + w - 20, 8, FONT_SANS, 15, C_DIM, "\xc3\x97", 2);
    }
    if (ntabs < 32) {
        int x, w; tab_geom(ntabs, &x, &w);
        fb_text(&fb, x + 6, 5, FONT_SANS, 20, C_ACCENT, "+", 1);
    }
    /* barra de herramientas */
    const char *glyph[4] = { "\xe2\x86\x90", "\xe2\x86\x92", "\xe2\x9f\xb3", "\xe2\x8c\x82" };
    int can[4] = { t->hi > 0, t->hi >= 0 && t->hi < t->nh - 1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        int x, y, w, h; btn_rect(i, &x, &y, &w, &h);
        fb_rrect(&fb, x, y, w, h, 6, C_TAB);
        int gw = text_w(FONT_SANS, 18, glyph[i], 3);
        fb_text(&fb, x + (w - gw) / 2, y + 4, FONT_SANS, 18, can[i] ? C_TEXT : 0x4a4f7a, glyph[i], 3);
    }
    int ux, uy, uw, uh; urlbar_rect(&ux, &uy, &uw, &uh);
    fb_rrect(&fb, ux, uy, uw, uh, 15, ufocus ? C_URLFOCUS : C_URL);
    if (ufocus) { fb_rrect(&fb, ux, uy, uw, 2, 1, C_PURPLE); }
    fb_clip(&fb, ux + 12, uy, uw - 24, uh);
    int ty = uy + (uh - UIPX - 4) / 2;
    int len = (int)strlen(ubuf);
    if (ufocus && usel && len) fb_rect(&fb, ux + 12, ty - 1, text_w(FONT_SANS, UIPX + 1, ubuf, len), UIPX + 5, 0x3a4aa8);
    fb_text(&fb, ux + 12, ty, FONT_SANS, UIPX + 1, ufocus ? C_TEXT : 0xbfc6f5, ubuf, len);
    if (ufocus && !usel && ((tick / 5) % 2 == 0)) {
        int cx = ux + 12 + text_w(FONT_SANS, UIPX + 1, ubuf, ucur);
        fb_rect(&fb, cx, ty - 1, 1, UIPX + 5, C_ACCENT);
    }
    fb_noclip(&fb);
    {   /* botón modo lectura */
        int rx, ry, rw2, rh; reader_rect(&rx, &ry, &rw2, &rh);
        fb_rrect(&fb, rx, ry, rw2, rh, 6, t->reader ? C_PURPLE : C_TAB);
        int gw = text_w(FONT_BOLD, 14, "Aa", 2);
        fb_text(&fb, rx + (rw2 - gw) / 2, ry + 7, FONT_BOLD, 14, t->reader ? 0xffffff : C_TEXT, "Aa", 2);
    }
    /* progreso */
    if (t->loading) {
        int seg = W / 4;
        int px = (int)((tick * 24) % (unsigned)(W + seg)) - seg;
        fb_clip(&fb, 0, TOP - 2, W, 2);
        fb_rect(&fb, px, TOP - 2, seg, 2, C_ACCENT);
        fb_noclip(&fb);
    } else fb_rect(&fb, 0, TOP - 1, W, 1, 0x1f2450);

    if (t->pl) {
        draw_player(t->pl, 0, TOP, W, vh);
        fb_rect(&fb, 0, H - STATH, W, STATH, C_STATUS);
        fb_rect(&fb, 0, H - STATH, W, 1, 0x1f2450);
        const char *hint = "Espacio pausa \xc2\xb7 \xe2\x86\x90/\xe2\x86\x92 \xc2\xb1""5s \xc2\xb7 J/L \xc2\xb1""10s \xc2\xb7 \xe2\x86\x91/\xe2\x86\x93 volumen \xc2\xb7 M silencio \xc2\xb7 C calidad (\xe2\x88\x92/+ sube o baja) \xc2\xb7 F pantalla completa";
        draw_text_fit(10, H - STATH + 4, FONT_SANS, 12, C_DIM, hint, W - 20);
        return;
    }
    /* página */
    fb_clip(&fb, 0, TOP, W, vh);
    uint32_t canvas = t->doc ? t->doc->canvas_bg : C_BG;
    fb_rect(&fb, 0, TOP, W, vh, canvas);
    Page *pg = &t->pg;
    for (int i = 0; i < pg->n; i++) {
        Item *it = &pg->it[i];
        int sy = it->y - t->scroll + TOP;
        if (sy + it->h < TOP || sy > TOP + vh) continue;
        switch (it->kind) {
        case IT_RECT: if (it->rad) fb_rrect(&fb, it->x, sy, it->w, it->h, it->rad, it->color); else fb_rect(&fb, it->x, sy, it->w, it->h, it->color); break;
        case IT_FRAME: fb_frame(&fb, it->x, sy, it->w, it->h, it->color); break;
        case IT_IMG: fb_image(&fb, it->img, it->x, sy, it->w, it->h); break;
        case IT_TEXT: {
            fb_text(&fb, it->x, sy, it->font, it->px, it->color, it->s, it->len);
            int asc = font_asc(it->font, it->px);
            int th = it->px >= 20 ? 2 : 1;
            if (it->ul) fb_rect(&fb, it->x, sy + asc + 2, it->w, th, it->color);
            if (it->st) fb_rect(&fb, it->x, sy + asc - it->px * 3 / 10, it->w, th, it->color);
            break;
        }
        }
    }
    if (!t->doc && !t->loading) {
        const char *m = "Pesta\xc3\xb1" "a vac\xc3\xad""a";
        fb_text(&fb, 20, TOP + 20, FONT_SANS, 16, C_DIM, m, (int)strlen(m));
    }
    /* barra de desplazamiento */
    if (pg->height > vh) {
        int th = (int)((long)vh * vh / pg->height);
        if (th < 24) th = 24;
        int maxs = pg->height - vh;
        int ty2 = TOP + (int)((long)(vh - th) * t->scroll / maxs);
        fb_rect(&fb, W - SBW, TOP, SBW, vh, 0x14173a);
        fb_rrect(&fb, W - SBW + 2, ty2 + 1, SBW - 4, th - 2, 3, 0x5c64b8);
    }
    fb_noclip(&fb);
    /* estado */
    fb_rect(&fb, 0, H - STATH, W, STATH, C_STATUS);
    fb_rect(&fb, 0, H - STATH, W, 1, 0x1f2450);
    const char *msg = hover_url[0] ? hover_url : (t->loading ? t->status : (t->reader ? "Modo lectura (F9 para salir)" : ""));
    char zbuf[32] = "";
    if (zoom != 1.0f) snprintf(zbuf, sizeof zbuf, "%d%%", (int)(zoom * 100 + 0.5f));
    char rbuf[96] = "";
    if (t->code >= 400 && !t->loading) snprintf(rbuf, sizeof rbuf, "HTTP %d", t->code);
    int rw = 0;
    char right[160];
    snprintf(right, sizeof right, "%s%s%s", rbuf, (rbuf[0] && zbuf[0]) ? "  " : "", zbuf);
    rw = text_w(FONT_SANS, 12, right, (int)strlen(right));
    if (right[0]) fb_text(&fb, W - rw - 10, H - STATH + 4, FONT_SANS, 12, C_ACCENT, right, (int)strlen(right));
    draw_text_fit(10, H - STATH + 4, FONT_SANS, 12, hover_url[0] ? 0xbde7ff : C_DIM, msg, W - rw - 40);
}

/* ================= entrada ================= */
static void url_insert(const char *s) {
    if (usel) { ubuf[0] = 0; ucur = 0; usel = 0; }
    int l = (int)strlen(ubuf), n = (int)strlen(s);
    if (l + n >= (int)sizeof ubuf - 1) return;
    memmove(ubuf + ucur + n, ubuf + ucur, l - ucur + 1);
    memcpy(ubuf + ucur, s, n);
    ucur += n;
}
static void url_backspace(void) {
    if (usel) { ubuf[0] = 0; ucur = 0; usel = 0; return; }
    if (ucur == 0) return;
    int p = ucur - 1;
    while (p > 0 && (ubuf[p] & 0xC0) == 0x80) p--;
    memmove(ubuf + p, ubuf + ucur, strlen(ubuf) - ucur + 1);
    ucur = p;
}
static void url_delete(void) {
    if (usel) { ubuf[0] = 0; ucur = 0; usel = 0; return; }
    int l = (int)strlen(ubuf);
    if (ucur >= l) return;
    int p = ucur + 1;
    while (p < l && (ubuf[p] & 0xC0) == 0x80) p++;
    memmove(ubuf + ucur, ubuf + p, l - p + 1);
}
static void url_left(void) {
    usel = 0;
    if (ucur == 0) return;
    ucur--;
    while (ucur > 0 && (ubuf[ucur] & 0xC0) == 0x80) ucur--;
}
static void url_right(void) {
    usel = 0;
    int l = (int)strlen(ubuf);
    if (ucur >= l) return;
    ucur++;
    while (ucur < l && (ubuf[ucur] & 0xC0) == 0x80) ucur++;
}

static void go_typed(int newtab) {
    char u[2048];
    omnibox_to_url(ubuf, u, sizeof u);
    ufocus = 0; usel = 0;
    if (newtab) tab_open(u); else tab_navigate(tabs[cur], u, 0);
    sync_urlbar();
}

static void nav_back(void) {
    Tab *t = tabs[cur];
    if (t->hi > 0) { t->hsc[t->hi] = t->scroll; t->hi--; tab_navigate(t, t->hist[t->hi], 2); sync_urlbar(); }
}
static void nav_fwd(void) {
    Tab *t = tabs[cur];
    if (t->hi >= 0 && t->hi < t->nh - 1) { t->hsc[t->hi] = t->scroll; t->hi++; tab_navigate(t, t->hist[t->hi], 2); sync_urlbar(); }
}

static void toggle_reader(void) {
    Tab *t = tabs[cur];
    if (!t->doc) return;
    t->reader = !t->reader;
    t->need_layout = 1;
    t->scroll = 0;
}

static void scroll_by(int dy) {
    Tab *t = tabs[cur];
    int vh = H - TOP - STATH, maxs = t->pg.height - vh;
    if (maxs < 0) maxs = 0;
    t->scroll += dy;
    if (t->scroll > maxs) t->scroll = maxs;
    if (t->scroll < 0) t->scroll = 0;
}

static void set_zoom(float z) {
    if (z < 0.5f) z = 0.5f;
    if (z > 3.0f) z = 3.0f;
    zoom = z;
    for (int i = 0; i < ntabs; i++) tabs[i]->need_layout = 1;
}

static void follow_link(const char *href, int newtab) {
    Tab *t = tabs[cur];
    char abs[2048];
    url_resolve(t->doc ? t->doc->base : t->url, href, abs, sizeof abs);
    const char *sc = scheme_of(abs);
    if (!strcmp(sc, "javascript") || !strcmp(sc, "mailto") || !strcmp(sc, "tel")) {
        LOCK; snprintf(t->status, sizeof t->status, "Enlace no soportado: %.60s", abs); UNLOCK;
        return;
    }
    if (newtab) tab_open(abs); else { tab_navigate(t, abs, 0); sync_urlbar(); }
}

static void resolve_hover(int mx, int my) {
    Tab *t = tabs[cur];
    hover_url[0] = 0;
    if (my < TOP || my >= H - STATH || mx >= W - SBW || !t->doc) return;
    Item *it = page_hit(&t->pg, mx, my - TOP + t->scroll);
    if (it && it->href) url_resolve(t->doc->base, it->href, hover_url, sizeof hover_url);
}

/* ================= captura (modo sin ventana) ================= */
static void save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", fb.w, fb.h);
    unsigned char *row = malloc((size_t)fb.w * 3);
    for (int y = 0; y < fb.h; y++) {
        for (int x = 0; x < fb.w; x++) {
            uint32_t p = fb.px[(size_t)y * fb.w + x];
            row[x * 3] = (p >> 16) & 255; row[x * 3 + 1] = (p >> 8) & 255; row[x * 3 + 2] = p & 255;
        }
        fwrite(row, 1, (size_t)fb.w * 3, f);
    }
    free(row);
    fclose(f);
}

static void wait_load(Tab *t, double maxsec) {
    for (double el = 0; el < maxsec; el += 0.02) {
        tab_poll(t);
        LOCK; int l = t->loading; UNLOCK;
        if (!l) break;
        usleep(20000);
    }
    tab_poll(t);
}

/* ================= SDL ================= */
#ifndef HEADLESS
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_Cursor *cur_arrow, *cur_hand, *cur_ibeam;
static int drag_sb;
static SDL_AudioDeviceID adev;
static void sdl_sink(const void *d, int n) {
    audio_bytes += n;
    if (adev) { while (SDL_GetQueuedAudioSize(adev) > 44100 * 4 * 2) usleep(10000); SDL_QueueAudio(adev, d, n); }
}
static void sdl_clear(void) { if (adev) SDL_ClearQueuedAudio(adev); }

static void make_texture(void) {
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
}

static void resize_to(int w, int h) {
    if (w < 320) w = 320;
    if (h < 240) h = 240;
    W = w; H = h;
    fb_resize(&fb, W, H);
    make_texture();
    for (int i = 0; i < ntabs; i++) tabs[i]->need_layout = 1;
}

static int hit(int mx, int my, int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; }

static void on_mouse_down(SDL_MouseButtonEvent *e) {
    int mx = e->x, my = e->y;
    Tab *t = tabs[cur];
    if (my < TABH) {
        for (int i = 0; i < ntabs; i++) {
            int x, w; tab_geom(i, &x, &w);
            if (hit(mx, my, x, 4, w, TABH - 4)) {
                if (e->button == SDL_BUTTON_MIDDLE || hit(mx, my, x + w - 24, 4, 24, TABH - 4)) { tab_close(i); if (!ntabs) { SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q); } }
                else { cur = i; ufocus = 0; sync_urlbar(); }
                return;
            }
        }
        int x, w; tab_geom(ntabs, &x, &w);
        if (hit(mx, my, x, 4, 30, TABH - 4)) tab_open("cosmos:home");
        return;
    }
    if (my < TOP) {
        for (int i = 0; i < 4; i++) {
            int x, y, w, h; btn_rect(i, &x, &y, &w, &h);
            if (hit(mx, my, x, y, w, h)) {
                if (i == 0) nav_back();
                else if (i == 1) nav_fwd();
                else if (i == 2) { tab_navigate(t, t->hist[t->hi >= 0 ? t->hi : 0], 1); }
                else tab_navigate(t, "cosmos:home", 0);
                sync_urlbar();
                return;
            }
        }
        int rx, ry, rw2, rh; reader_rect(&rx, &ry, &rw2, &rh);
        if (hit(mx, my, rx, ry, rw2, rh)) { toggle_reader(); return; }
        int ux, uy, uw, uh; urlbar_rect(&ux, &uy, &uw, &uh);
        if (hit(mx, my, ux, uy, uw, uh)) {
            if (!ufocus) { ufocus = 1; usel = 1; ucur = (int)strlen(ubuf); }
            else {
                usel = 0;
                int target = mx - (ux + 12), best = 0, len = (int)strlen(ubuf);
                const char *p = ubuf;
                while (p < ubuf + len) {
                    const char *q = p; utf8_next(&q, ubuf + len);
                    int wpre = text_w(FONT_SANS, UIPX + 1, ubuf, (int)(q - ubuf));
                    if (wpre > target + 2) break;
                    best = (int)(q - ubuf); p = q;
                }
                ucur = best;
            }
        } else if (ufocus) { ufocus = 0; usel = 0; sync_urlbar(); }
        return;
    }
    if (ufocus) { ufocus = 0; usel = 0; sync_urlbar(); }
    int vh = H - TOP - STATH;
    if (t->pl) {
        if (e->button != SDL_BUTTON_LEFT) return;
        int cy = pl_ry + pl_rh - CTRLH;
        if (my >= cy && my < cy + 26) {
            double dur = player_dur(t->pl);
            int bx = pl_rx + 16, bw = pl_rw - 32;
            if (dur > 0 && mx >= bx - 4 && mx <= bx + bw + 4) {
                double f = (double)(mx - bx) / bw;
                player_seek_abs(t->pl, (f < 0 ? 0 : f > 1 ? 1 : f) * dur);
            }
        } else if (my < cy) player_toggle_pause(t->pl);
        return;
    }
    if (my >= TOP && my < TOP + vh) {
        if (mx >= W - SBW && t->pg.height > vh) { drag_sb = 1; return; }
        if (t->doc) {
            Item *it = page_hit(&t->pg, mx, my - TOP + t->scroll);
            int prevented = 0;
            if (t->doc->js && e->button == SDL_BUTTON_LEFT) {
                Item *hn = page_hit_node(&t->pg, mx, my - TOP + t->scroll);
                if (hn && hn->nd) { prevented = js_dispatch(t->doc, hn->nd, "click"); tab_js_sync(t); }
            }
            if (!prevented && it && it->href && (e->button == SDL_BUTTON_LEFT || e->button == SDL_BUTTON_MIDDLE))
                follow_link(it->href, e->button == SDL_BUTTON_MIDDLE || (SDL_GetModState() & KMOD_CTRL));
        }
    }
}

static void sb_drag(int my) {
    Tab *t = tabs[cur];
    int vh = H - TOP - STATH, maxs = t->pg.height - vh;
    if (maxs <= 0) return;
    int th = (int)((long)vh * vh / t->pg.height);
    if (th < 24) th = 24;
    int span = vh - th;
    if (span <= 0) return;
    int pos = my - TOP - th / 2;
    t->scroll = (int)((long)pos * maxs / span);
    scroll_by(0);
}

static void on_key(SDL_KeyboardEvent *e) {
    SDL_Keycode k = e->keysym.sym;
    int ctrl = (e->keysym.mod & KMOD_CTRL) != 0, alt = (e->keysym.mod & KMOD_ALT) != 0;
    Tab *t = tabs[cur];
    int vh = H - TOP - STATH;
    if (ctrl) {
        switch (k) {
        case SDLK_t: tab_open("cosmos:home"); ufocus = 1; usel = 1; ubuf[0] = 0; ucur = 0; return;
        case SDLK_w: tab_close(cur); if (!ntabs) { SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q); } return;
        case SDLK_l: ufocus = 1; usel = 1; ucur = (int)strlen(ubuf); return;
        case SDLK_r: tab_navigate(t, t->hist[t->hi >= 0 ? t->hi : 0], 1); sync_urlbar(); return;
        case SDLK_e: toggle_reader(); return;
        case SDLK_q: { SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q); return; }
        case SDLK_TAB: cur = (cur + ((e->keysym.mod & KMOD_SHIFT) ? ntabs - 1 : 1)) % ntabs; ufocus = 0; sync_urlbar(); return;
        case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: set_zoom(zoom + (zoom >= 1.5f ? 0.25f : 0.1f)); return;
        case SDLK_MINUS: case SDLK_KP_MINUS: set_zoom(zoom - (zoom > 1.5f ? 0.25f : 0.1f)); return;
        case SDLK_0: case SDLK_KP_0: set_zoom(1.0f); return;
        case SDLK_a: if (ufocus) { usel = 1; ucur = (int)strlen(ubuf); } return;
        case SDLK_v: if (ufocus) { char *c = SDL_GetClipboardText(); if (c) { for (char *p = c; *p; p++) if (*p == '\n' || *p == '\r') *p = ' '; url_insert(c); SDL_free(c); } } return;
        case SDLK_c: if (ufocus) SDL_SetClipboardText(ubuf); else SDL_SetClipboardText(t->url); return;
        case SDLK_x: if (ufocus) { SDL_SetClipboardText(ubuf); ubuf[0] = 0; ucur = 0; usel = 0; } return;
        default: break;
        }
    }
    if (alt) {
        if (k == SDLK_LEFT) { nav_back(); return; }
        if (k == SDLK_RIGHT) { nav_fwd(); return; }
        if (k == SDLK_HOME) { tab_navigate(t, "cosmos:home", 0); sync_urlbar(); return; }
    }
    if (k == SDLK_F5) { tab_navigate(t, t->hist[t->hi >= 0 ? t->hi : 0], 1); sync_urlbar(); return; }
    if (k == SDLK_F9) { toggle_reader(); return; }
    if (k == SDLK_F6) { ufocus = 1; usel = 1; ucur = (int)strlen(ubuf); return; }
    if (t->pl && !ufocus) {
        switch (k) {
        case SDLK_SPACE: case SDLK_k: player_toggle_pause(t->pl); return;
        case SDLK_LEFT: player_seek(t->pl, -5); return;
        case SDLK_RIGHT: player_seek(t->pl, 5); return;
        case SDLK_j: player_seek(t->pl, -10); return;
        case SDLK_l: player_seek(t->pl, 10); return;
        case SDLK_UP: player_volume(t->pl, 5); return;
        case SDLK_DOWN: player_volume(t->pl, -5); return;
        case SDLK_m: player_mute(t->pl); return;
        case SDLK_c: player_set_quality(t->pl, (e->keysym.mod & KMOD_SHIFT) ? -1 : 2); return;
        case SDLK_MINUS: case SDLK_KP_MINUS: player_set_quality(t->pl, -1); return;
        case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS: player_set_quality(t->pl, 1); return;
        case SDLK_LEFTBRACKET: player_set_quality(t->pl, -1); return;
        case SDLK_RIGHTBRACKET: player_set_quality(t->pl, 1); return;
        case SDLK_f: case SDLK_F11:
            fullvid = !fullvid;
            SDL_SetWindowFullscreen(win, fullvid ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            return;
        case SDLK_ESCAPE: if (fullvid) { fullvid = 0; SDL_SetWindowFullscreen(win, 0); return; } break;
        default: break;
        }
    }
    if (ufocus) {
        switch (k) {
        case SDLK_RETURN: case SDLK_KP_ENTER: go_typed(ctrl || (e->keysym.mod & KMOD_SHIFT)); break;
        case SDLK_ESCAPE: ufocus = 0; usel = 0; sync_urlbar(); break;
        case SDLK_BACKSPACE: url_backspace(); break;
        case SDLK_DELETE: url_delete(); break;
        case SDLK_LEFT: url_left(); break;
        case SDLK_RIGHT: url_right(); break;
        case SDLK_HOME: usel = 0; ucur = 0; break;
        case SDLK_END: usel = 0; ucur = (int)strlen(ubuf); break;
        default: break;
        }
        return;
    }
    switch (k) {
    case SDLK_DOWN: scroll_by(48); break;
    case SDLK_UP: scroll_by(-48); break;
    case SDLK_PAGEDOWN: scroll_by(vh - 40); break;
    case SDLK_PAGEUP: scroll_by(-(vh - 40)); break;
    case SDLK_SPACE: scroll_by((e->keysym.mod & KMOD_SHIFT) ? -(vh - 40) : (vh - 40)); break;
    case SDLK_HOME: t->scroll = 0; break;
    case SDLK_END: scroll_by(1 << 28); break;
    case SDLK_BACKSPACE: nav_back(); break;
    case SDLK_ESCAPE: LOCK; t->gen++; t->loading = 0; t->status[0] = 0; UNLOCK; break;
    default: break;
    }
}

static int run_gui(int argc, char **argv, int first) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("Cosmos Browser", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "ventana: %s\n", SDL_GetError()); return 1; }
    {   /* icono de la ventana */
        char ip[1100]; size_t il;
        snprintf(ip, sizeof ip, "%s/icon.png", exe_dir);
        uint8_t *idata = read_file(ip, &il);
        Image *ic = idata ? image_decode(idata, il) : NULL;
        if (ic) {
            SDL_Surface *sf = SDL_CreateRGBSurfaceFrom(ic->px, ic->w, ic->h, 32, ic->w * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            if (sf) { SDL_SetWindowIcon(win, sf); SDL_FreeSurface(sf); }
            image_free(ic);
        }
        free(idata);
    }
    ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_GetWindowSize(win, &W, &H);
    fb_init(&fb, W, H);
    make_texture();
    cur_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cur_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    cur_ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    SDL_StartTextInput();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0); else fprintf(stderr, "audio: %s\n", SDL_GetError());
    } else fprintf(stderr, "audio: %s\n", SDL_GetError());
    player_audio_sink = sdl_sink;
    player_audio_clear = sdl_clear;
    if (first >= argc) tab_open("cosmos:home");
    else for (int i = first; i < argc; i++) { char u[2048]; omnibox_to_url(argv[i], u, sizeof u); tab_open(u); }
    cur = 0;
    sync_urlbar();
    int running = 1, redraw = 1;
    Uint32 last_tick = SDL_GetTicks(), last_js = SDL_GetTicks();
    char last_title[300] = "";
    while (running) {
        SDL_Event ev;
        int got = SDL_WaitEventTimeout(&ev, 40);
        while (got) {
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int w, h; SDL_GetWindowSize(win, &w, &h);
                    if (w != W || h != H) resize_to(w, h);
                }
                redraw = 1; break;
            case SDL_KEYDOWN: if (ntabs) on_key(&ev.key); redraw = 1; break;
            case SDL_TEXTINPUT: if (ufocus && !(SDL_GetModState() & KMOD_CTRL)) { url_insert(ev.text.text); redraw = 1; } break;
            case SDL_MOUSEBUTTONDOWN: if (ntabs) on_mouse_down(&ev.button); redraw = 1; break;
            case SDL_MOUSEBUTTONUP: drag_sb = 0; break;
            case SDL_MOUSEMOTION: {
                if (!ntabs) break;
                if (drag_sb) { sb_drag(ev.motion.y); redraw = 1; break; }
                char before[2048]; snprintf(before, sizeof before, "%s", hover_url);
                resolve_hover(ev.motion.x, ev.motion.y);
                int urlzone = ev.motion.y >= TABH && ev.motion.y < TOP && ev.motion.x > 8 + 4 * 34 + 4;
                SDL_SetCursor(hover_url[0] ? cur_hand : (urlzone ? cur_ibeam : cur_arrow));
                if (strcmp(before, hover_url)) redraw = 1;
                break;
            }
            case SDL_MOUSEWHEEL: {
                if (!ntabs) break;
                if (SDL_GetModState() & KMOD_CTRL) set_zoom(zoom + (ev.wheel.y > 0 ? 0.1f : -0.1f));
                else scroll_by(-ev.wheel.y * 54);
                redraw = 1; break;
            }
            case SDL_DROPFILE: { char u[2048]; omnibox_to_url(ev.drop.file, u, sizeof u); tab_open(u); SDL_free(ev.drop.file); redraw = 1; break; }
            }
            got = SDL_PollEvent(&ev);
        }
        if (!ntabs) break;
        for (int i = 0; i < ntabs; i++) if (tab_poll(tabs[i]) && i == cur) redraw = 1;
        Tab *t = tabs[cur];
        Uint32 now = SDL_GetTicks();
        if ((t->loading || ufocus) && now - last_tick > 100) { tick++; last_tick = now; redraw = 1; }
        if (t->pl) {
            if (player_new_frame(t->pl)) redraw = 1;
            if (now - last_tick > 250) { last_tick = now; tick++; redraw = 1; }
        }
        if (t->doc && t->doc->js) {
            if (now - last_js >= 50) {
                int dt = (int)(now - last_js); if (dt > 500) dt = 500;
                last_js = now;
                js_tick(t->doc, dt);
            }
            char *nv = js_take_nav(t->doc);
            if (nv) { follow_link(nv, 0); free(nv); redraw = 1; }
            else { int g0 = t->seen_gen; tab_js_sync(t); if (t->seen_gen != g0) redraw = 1; }
        }
        if (t->need_layout) { tab_layout(t); tab_scroll_to_frag(t); redraw = 1; }
        if (t->loading && !t->doc) redraw = redraw;
        if (redraw) {
            draw_ui();
            SDL_UpdateTexture(tex, NULL, fb.px, fb.w * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            char title[300];
            snprintf(title, sizeof title, "%s - Cosmos Browser", tab_title(t));
            if (strcmp(title, last_title)) { SDL_SetWindowTitle(win, title); snprintf(last_title, sizeof last_title, "%s", title); }
            redraw = 0;
        }
    }
    SDL_Quit();
    return 0;
}
#endif

/* ================= main ================= */
static void count_sink(const void *d, int n) { (void)d; audio_bytes += n; }

static void usage(void) {
    printf("Cosmos Browser %s\n"
           "uso: cosmos [URL|b\xc3\xbasqueda|archivo ...]\n"
           "     cosmos --shot salida.ppm [--size AxB] [--scroll N] [--zoom Z] [--reader] [--tabs] URL   (captura sin ventana)\n", COSMOS_VERSION);
}

int main(int argc, char **argv) {
    ssize_t el = readlink("/proc/self/exe", exe_dir, sizeof exe_dir - 1);
    if (el > 0) { exe_dir[el] = 0; char *sl = strrchr(exe_dir, '/'); if (sl) *sl = 0; } else strcpy(exe_dir, ".");
    char fontdir[PATH_MAX + 16];
    snprintf(fontdir, sizeof fontdir, "%s/fonts", exe_dir);
    snprintf(cosmos_dir, sizeof cosmos_dir, "%s", exe_dir);
    if (gfx_init(fontdir) != 0) { fprintf(stderr, "No encontr\xc3\xa9 las fuentes DejaVu (carpeta fonts/).\n"); return 1; }
    const char *shot = NULL;
    int scroll = 0, extra_tabs = 0, first = 1, want_reader = 0;
    double waitv = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) { shot = argv[++i]; first = i + 1; }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &W, &H); first = i + 1; }
        else if (!strcmp(argv[i], "--scroll") && i + 1 < argc) { scroll = atoi(argv[++i]); first = i + 1; }
        else if (!strcmp(argv[i], "--zoom") && i + 1 < argc) { zoom = (float)atof(argv[++i]); first = i + 1; }
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) { waitv = atof(argv[++i]); first = i + 1; }
        else if (!strcmp(argv[i], "--tabs")) { extra_tabs = 1; first = i + 1; }
        else if (!strcmp(argv[i], "--reader")) { want_reader = 1; first = i + 1; }
        else if (argv[i][0] != '-') { first = i; break; }
    }
    if (shot) {
        player_audio_sink = count_sink;
        fb_init(&fb, W, H);
        char u[2048];
        omnibox_to_url(first < argc ? argv[first] : "cosmos:home", u, sizeof u);
        tab_open(u);
        if (extra_tabs) { tab_open("cosmos:about"); wait_load(tabs[1], 30); cur = 0; sync_urlbar(); }
        wait_load(tabs[0], 60);
        if (tabs[0]->pl) {
            usleep((useconds_t)(waitv * 1e6));
            if (getenv("COSMOS_TEST_SEEK")) { player_seek(tabs[0]->pl, atof(getenv("COSMOS_TEST_SEEK"))); usleep(2500000); }
            if (getenv("COSMOS_TEST_QUAL")) { player_set_quality(tabs[0]->pl, atoi(getenv("COSMOS_TEST_QUAL"))); usleep(2500000); fprintf(stderr, "calidad=%dp\n", player_quality(tabs[0]->pl)); }
            char tt[256], ee[512];
            player_info(tabs[0]->pl, tt, sizeof tt, ee, sizeof ee);
            fprintf(stderr, "player: estado=%d pos=%.2f dur=%.1f audio_bytes=%ld titulo=\"%s\" err=\"%s\"\n",
                    player_state(tabs[0]->pl), player_pos(tabs[0]->pl), player_dur(tabs[0]->pl), audio_bytes, tt, ee);
        }
        tabs[0]->reader = want_reader;
        tab_layout(tabs[0]);
        if (tabs[0]->doc && tabs[0]->doc->js) {
            Tab *t0 = tabs[0];
            const char *cl = getenv("COSMOS_TEST_CLICK");   /* "x,y;x,y" en coordenadas de página */
            while (cl && *cl) {
                int cx, cy, used = 0;
                if (sscanf(cl, "%d,%d%n", &cx, &cy, &used) < 2) break;
                Item *hn = page_hit_node(&t0->pg, cx, cy);
                int pv = hn && hn->nd ? js_dispatch(t0->doc, hn->nd, "click") : -1;
                fprintf(stderr, "clic %d,%d -> nodo=%s prevented=%d\n", cx, cy, hn && hn->nd ? hn->nd->name : "(ninguno)", pv);
                tab_js_sync(t0); tab_layout(t0);
                cl += used; if (*cl == ';') cl++;
            }
            if (waitv > 0) { js_tick(t0->doc, (int)(waitv * 1000)); tab_js_sync(t0); tab_layout(t0); }
            char *nv = js_take_nav(t0->doc);
            if (nv) { fprintf(stderr, "navegaci\xc3\xb3n pedida: %s\n", nv); free(nv); }
        }
        tab_scroll_to_frag(tabs[0]);
        tabs[0]->scroll = scroll;
        scroll_by(0);
        draw_ui();
        save_ppm(shot);
        return 0;
    }
#ifdef HEADLESS
    fprintf(stderr, "Esta compilaci\xc3\xb3n es solo para capturas (--shot).\n");
    return 1;
#else
    return run_gui(argc, argv, first);
#endif
}
