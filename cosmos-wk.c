#define _GNU_SOURCE
/* Cosmos Browser (edición WebKit) - interfaz GTK3 sobre WebKitGTK 4.1.
 * Compilar: make cosmos-wk     Dependencias (Arch): gtk3 webkit2gtk-4.1 */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <webkit2/webkit2.h>
#include "wk-data.h"
#include "wk-import.h"
#include "wk-i18n.h"

#define VERSION "0.8-webkit"
#define HOME_URL "cosmos://home"

static GtkWidget *win, *nb, *entry, *status, *btn_back, *btn_fwd, *btn_reload;
static WebKitWebContext *ctx;
static WebKitSettings *settings;
static WebKitWebContext *ictx;            /* contexto efímero (navegación privada), se crea al usarse */
static GtkWidget *btn_star;
static void act_downloads(void);
static void on_lang_item(GtkCheckMenuItem *mi, gpointer data);
static GtkEntryCompletion *comp;
static GtkListStore *comp_store;
static int vault_declined;
static char tok[33];                      /* token anti-CSRF de las páginas cosmos:// */
static char exedir[1024] = ".";
static int fullscreen_on;
static int lang;                           /* índice en LANG_CODE (0 = inglés, por defecto) */
static const char *T(int k) { return TR[lang][k] && *TR[lang][k] ? TR[lang][k] : TR[0][k]; }
static int lang_rtl(void) { return !strcmp(LANG_CODE[lang], "ar"); }
static void lang_load(void) {
    lang = 0;
    char *p = data_path("settings.conf"), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char *l = strstr(txt, "lang=");
        if (l) { l += 5; for (int i = 0; i < LANG_N; i++) if (!strncmp(l, LANG_CODE[i], strlen(LANG_CODE[i]))) lang = i; }
        g_free(txt);
    }
    g_free(p);
    const char *e = g_getenv("COSMOS_LANG");               /* solo para probar, no se guarda */
    if (e) for (int i = 0; i < LANG_N; i++) if (!strcmp(e, LANG_CODE[i])) lang = i;
}
static void lang_save(void) {
    char *p = data_path("settings.conf"), *t = g_strdup_printf("lang=%s\n", LANG_CODE[lang]);
    g_file_set_contents(p, t, -1, NULL); g_chmod(p, 0600); g_free(p); g_free(t);
}

/* ---------- utilidades ---------- */
static char *read_whole(const char *name, gsize *len) {
    char *p = g_build_filename(exedir, name, NULL), *data = NULL;
    if (!g_file_get_contents(p, &data, len, NULL)) data = NULL;
    g_free(p);
    return data;
}

static char *omni(const char *in) {
    char *s = g_strstrip(g_strdup(in));
    char *out;
    if (!*s) out = g_strdup(HOME_URL);
    else if (g_str_has_prefix(s, "cosmos:")) {
        const char *r = s + 7; while (*r == '/') r++;
        out = g_strdup_printf("cosmos://%s", *r ? r : "home");
    } else if (strstr(s, "://") || g_str_has_prefix(s, "about:") || g_str_has_prefix(s, "data:")) out = g_strdup(s);
    else if (s[0] == '/') out = g_strdup_printf("file://%s", s);
    else if (g_str_has_prefix(s, "yt ") || g_str_has_prefix(s, "youtube ")) {
        char *q = g_uri_escape_string(strchr(s, ' ') + 1, NULL, FALSE);
        out = g_strdup_printf("https://www.youtube.com/results?search_query=%s", q); g_free(q);
    } else if (!strchr(s, ' ') && (strchr(s, '.') || g_str_has_prefix(s, "localhost"))) out = g_strdup_printf("https://%s", s);
    else {
        char *q = g_uri_escape_string(s, NULL, FALSE);
        out = g_strdup_printf("https://duckduckgo.com/?q=%s", q); g_free(q);
    }
    g_free(s);
    return out;
}

/* ---------- páginas internas cosmos:// ---------- */
static const char *CSS_BASE =
    "body{margin:0;background:#07091a;color:#dfe3ff;font-family:sans-serif;text-align:center}"
    "a{color:#5de0ff}h1{margin:.2em 0}.box{max-width:640px;margin:0 auto;padding:30px 16px}"
    "input{width:80%;padding:12px 16px;border-radius:24px;border:1px solid #3a4280;background:#12163a;color:#fff;font-size:17px}"
    "small{color:#8a93d6}table{width:100%;max-width:980px;margin:12px auto;border-collapse:collapse;text-align:start}"
    "td{padding:6px 8px;border-bottom:1px solid #1d2252;font-size:14px}td.t{color:#8a93d6;white-space:nowrap;width:1%}"
    "td a.u{color:#7f88c8;font-size:12px;display:block}.act{color:#ff8aa0}.warn{background:#2a1430;border:1px solid #6b2b5e;border-radius:10px;padding:10px;max-width:640px;margin:12px auto}";

static GString *page_head(const char *title) {
    GString *g = g_string_new("");
    char *t = g_markup_escape_text(title, -1);
    g_string_append_printf(g, "<html lang=%s dir=%s><head><meta charset=utf-8><title>%s</title><style>%s</style></head><body><div class=box>"
        "<h1>%s</h1><p><small><a href=cosmos://home>%s</a> &middot; <a href=cosmos://history>%s</a> &middot; "
        "<a href=cosmos://bookmarks>%s</a> &middot; <a href=cosmos://passwords>%s</a></small></p></div>",
        LANG_CODE[lang], lang_rtl() ? "rtl" : "ltr", t, CSS_BASE, t, T(K_T_HOME), T(K_M_HISTORY), T(K_M_BOOKMARKS), T(K_M_PASSWORDS));
    g_free(t);
    return g;
}
static const char *qparam(const char *query, const char *key, char **out) {
    *out = NULL;
    if (!query) return NULL;
    char **parts = g_strsplit(query, "&", -1);
    size_t kl = strlen(key);
    for (int i = 0; parts[i]; i++)
        if (!strncmp(parts[i], key, kl) && parts[i][kl] == '=') { *out = g_uri_unescape_string(parts[i] + kl + 1, NULL); break; }
    g_strfreev(parts);
    return *out;
}
static void esc_app(GString *g, const char *s) { char *e = g_markup_escape_text(s ? s : "", -1); g_string_append(g, e); g_free(e); }

static char *build_history(const char *query) {
    char *q = NULL, *clr = NULL, *t = NULL;
    qparam(query, "q", &q); qparam(query, "clear", &clr); qparam(query, "t", &t);
    if (clr && t && !strcmp(t, tok)) hist_clear();
    GString *g = page_head(T(K_M_HISTORY));
    g_string_append_printf(g, "<div class=box><form><input name=q placeholder=\"%s\" value=\"", T(K_HIST_SEARCH));
    esc_app(g, q); g_string_append(g, "\"></form>");
    g_string_append_printf(g, "<p><small><a class=act href=\"cosmos://history?clear=1&t=%s\">%s</a></small></p></div><table>", tok, T(K_HIST_CLEAR));
    int shown = 0;
    for (int i = (int)hist->len - 1; i >= 0 && shown < 500; i--) {
        HEntry *e = g_ptr_array_index(hist, i);
        if (q && *q && !strcasestr(e->url, q) && !strcasestr(e->title, q)) continue;
        char ts[32]; time_t tt = e->t; strftime(ts, sizeof ts, "%d/%m %H:%M", localtime(&tt));
        g_string_append_printf(g, "<tr><td class=t>%s</td><td><a href=\"", ts); esc_app(g, e->url); g_string_append(g, "\">");
        esc_app(g, *e->title ? e->title : e->url); g_string_append(g, "</a><a class=u>"); esc_app(g, e->url); g_string_append(g, "</a></td></tr>");
        shown++;
    }
    if (!shown) g_string_append_printf(g, "<tr><td>%s</td></tr>", T(K_HIST_EMPTY));
    g_string_append(g, "</table></body></html>");
    g_free(q); g_free(clr); g_free(t);
    return g_string_free(g, FALSE);
}
static char *build_bookmarks(const char *query) {
    char *del = NULL, *t = NULL;
    qparam(query, "del", &del); qparam(query, "t", &t);
    if (del && t && !strcmp(t, tok)) bm_remove(del);
    GString *g = page_head(T(K_M_BOOKMARKS));
    g_string_append(g, "<table>");
    for (guint i = 0; i < bms->len; i++) {
        BEntry *e = g_ptr_array_index(bms, i);
        char *enc = g_uri_escape_string(e->url, NULL, TRUE);
        g_string_append(g, "<tr><td><a href=\""); esc_app(g, e->url); g_string_append(g, "\">"); esc_app(g, e->title);
        g_string_append(g, "</a><a class=u>"); esc_app(g, e->url);
        g_string_append_printf(g, "</a></td><td class=t><a class=act href=\"cosmos://bookmarks?del=%s&t=%s\">%s</a></td></tr>", enc, tok, T(K_REMOVE));
        g_free(enc);
    }
    if (!bms->len) g_string_append_printf(g, "<tr><td>%s</td></tr>", T(K_BM_EMPTY));
    g_string_append(g, "</table></body></html>");
    g_free(del); g_free(t);
    return g_string_free(g, FALSE);
}
static char *ask_master(const char *title, const char *msg, gboolean confirm);
static int ensure_vault(gboolean create_if_missing);
static char *build_passwords(const char *query) {
    char *del = NULL, *t = NULL;
    qparam(query, "del", &del); qparam(query, "t", &t);
    GString *g = page_head(T(K_M_PASSWORDS));
    if (!vunlocked) {
        g_string_append_printf(g, "<div class=warn>%s</div></body></html>", T(K_PW_LOCKED_MSG));
    } else {
        if (del && t && !strcmp(t, tok)) cred_remove((guint)atoi(del));
        g_string_append(g, "<table>");
        for (guint i = 0; i < creds->len; i++) {
            Cred *c = g_ptr_array_index(creds, i);
            g_string_append(g, "<tr><td>"); esc_app(g, c->origin); g_string_append(g, "</td><td>"); esc_app(g, c->user);
            g_string_append_printf(g, "</td><td class=t><a class=act href=\"cosmos://passwords?del=%u&t=%s\">%s</a></td></tr>", i, tok, T(K_DELETE));
        }
        if (!creds->len) g_string_append_printf(g, "<tr><td>%s</td></tr>", T(K_PW_EMPTY));
        g_string_append_printf(g, "</table><div class=box><small>%s</small></div></body></html>", T(K_PW_NOTE));
    }
    g_free(del); g_free(t);
    return g_string_free(g, FALSE);
}

static void scheme_cb(WebKitURISchemeRequest *req, gpointer ud) {
    const char *uri = webkit_uri_scheme_request_get_uri(req);
    char *path = g_strdup(uri + strlen("cosmos://"));
    char *query = NULL, *qm = strchr(path, '?');
    if (qm) { *qm = 0; query = qm + 1; }
    char *body = NULL; gsize len = 0; const char *mime = "text/html";
    if (!strcmp(path, "logo.png") || !strcmp(path, "logo.png/")) { body = read_whole("logo.png", &len); mime = "image/png"; }
    else if (!strcmp(path, "icon.png")) { body = read_whole("icon.png", &len); mime = "image/png"; }
    else if (!strncmp(path, "history", 7)) body = build_history(query);
    else if (!strncmp(path, "bookmarks", 9)) body = build_bookmarks(query);
    else if (!strncmp(path, "passwords", 9)) body = build_passwords(query);
    else if (!strncmp(path, "about", 5)) {
        char *eng = g_strdup_printf(T(K_ABOUT_ENGINE), webkit_get_major_version(), webkit_get_minor_version(), webkit_get_micro_version());
        body = g_strdup_printf("<html lang=%s dir=%s><head><meta charset=utf-8><title>%s</title><style>%s</style></head><body><div class=box>"
            "<img src=cosmos://logo.png width=170><h1>Cosmos Browser " VERSION "</h1><p>%s</p><p><small>%s</small></p>"
            "<p><a href=cosmos://home>%s</a></p></div></body></html>",
            LANG_CODE[lang], lang_rtl() ? "rtl" : "ltr", T(K_T_ABOUT), CSS_BASE, eng, T(K_SHORTCUTS), T(K_T_HOME));
        g_free(eng);
    } else {
        body = g_strdup_printf("<html lang=%s dir=%s><head><meta charset=utf-8><title>Cosmos Browser</title><style>%s</style></head><body><div class=box>"
            "<img src=cosmos://logo.png width=190><h1>Cosmos Browser</h1><p><small>%s</small></p>"
            "<form action=\"https://duckduckgo.com/\"><input name=q placeholder=\"%s\" autofocus></form>"
            "<p><a href=\"https://www.youtube.com\">YouTube</a> &middot; <a href=\"https://%s.wikipedia.org\">Wikipedia</a> &middot; "
            "<a href=\"https://duckduckgo.com\">DuckDuckGo</a></p>"
            "<p><small><a href=cosmos://history>%s</a> &middot; <a href=cosmos://bookmarks>%s</a> &middot; "
            "<a href=cosmos://passwords>%s</a> &middot; <a href=cosmos://about>%s</a></small></p></div></body></html>",
            LANG_CODE[lang], lang_rtl() ? "rtl" : "ltr", CSS_BASE, T(K_HOME_SUB), T(K_HOME_SEARCH), LANG_CODE[lang],
            T(K_M_HISTORY), T(K_M_BOOKMARKS), T(K_M_PASSWORDS), T(K_T_ABOUT));
    }
    if (body && !len) len = strlen(body);
    if (!body) { body = g_strdup("no encontrado"); len = strlen(body); mime = "text/plain"; }
    GInputStream *st = g_memory_input_stream_new_from_data(body, len, g_free);
    webkit_uri_scheme_request_finish(req, st, len, mime);
    g_object_unref(st);
    g_free(path);
}

/* ---------- pestañas ---------- */
static WebKitWebView *cur_view(void) {
    int p = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
    return p < 0 ? NULL : WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), p));
}
static GtkWidget *tab_label(GtkWidget *view) { return g_object_get_data(G_OBJECT(view), "label"); }

static void update_nav(void) {
    WebKitWebView *v = cur_view();
    gtk_widget_set_sensitive(btn_back, v && webkit_web_view_can_go_back(v));
    gtk_widget_set_sensitive(btn_fwd, v && webkit_web_view_can_go_forward(v));
    gtk_button_set_label(GTK_BUTTON(btn_reload), v && webkit_web_view_is_loading(v) ? "\xe2\x9c\x95" : "\xe2\x9f\xb3");
}
static void sync_chrome(void) {
    WebKitWebView *v = cur_view();
    if (!v) return;
    const char *u = webkit_web_view_get_uri(v), *t = webkit_web_view_get_title(v);
    if (!gtk_widget_has_focus(entry)) {
        char *shown = (u && g_str_has_prefix(u, "cosmos://")) ? g_strdup_printf("cosmos:%s", u + 9) : g_strdup(u ? u : "");
        gtk_entry_set_text(GTK_ENTRY(entry), shown); g_free(shown);
    }
    char *wt = g_strdup_printf("%s - Cosmos Browser", t && *t ? t : T(K_NEW_TAB));
    gtk_window_set_title(GTK_WINDOW(win), wt); g_free(wt);
    gtk_entry_set_progress_fraction(GTK_ENTRY(entry), webkit_web_view_is_loading(v) ? webkit_web_view_get_estimated_load_progress(v) : 0.0);
    update_nav();
    int marked = u && bm_find(u) >= 0;
    gtk_button_set_label(GTK_BUTTON(btn_star), marked ? "\xe2\x98\x85" : "\xe2\x98\x86");
}

static void on_title(WebKitWebView *v, GParamSpec *p, gpointer d) {
    const char *t = webkit_web_view_get_title(v);
    gtk_label_set_text(GTK_LABEL(tab_label(GTK_WIDGET(v))), t && *t ? t : T(K_LOADING));
    if (v == cur_view()) sync_chrome();
}
static void on_any(WebKitWebView *v, GParamSpec *p, gpointer d) { if (v == cur_view()) sync_chrome(); }
static void on_hover(WebKitWebView *v, WebKitHitTestResult *h, guint mods, gpointer d) {
    gtk_label_set_text(GTK_LABEL(status), webkit_hit_test_result_context_is_link(h) ? webkit_hit_test_result_get_link_uri(h) : "");
}
/* ---------- contraseñas: diálogos y manejo de mensajes de las páginas ---------- */
static int is_incog(GtkWidget *v) { return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(v), "incog")); }

static char *ask_master(const char *title, const char *msg, gboolean confirm) {
    if (getenv("COSMOS_TEST_MASTER")) return g_strdup(getenv("COSMOS_TEST_MASTER"));   /* solo pruebas */
    GtkWidget *dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(win), GTK_DIALOG_MODAL,
        T(K_CANCEL), GTK_RESPONSE_CANCEL, T(K_OK), GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    GtkWidget *lab = gtk_label_new(msg); gtk_label_set_line_wrap(GTK_LABEL(lab), TRUE); gtk_label_set_max_width_chars(GTK_LABEL(lab), 48);
    GtkWidget *e1 = gtk_entry_new(), *e2 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(e1), FALSE); gtk_entry_set_visibility(GTK_ENTRY(e2), FALSE);
    gtk_entry_set_activates_default(GTK_ENTRY(e1), !confirm); gtk_entry_set_activates_default(GTK_ENTRY(e2), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(e1), T(K_MASTER)); gtk_entry_set_placeholder_text(GTK_ENTRY(e2), T(K_MASTER2));
    gtk_box_pack_start(GTK_BOX(area), lab, FALSE, FALSE, 6); gtk_box_pack_start(GTK_BOX(area), e1, FALSE, FALSE, 4);
    if (confirm) gtk_box_pack_start(GTK_BOX(area), e2, FALSE, FALSE, 4);
    gtk_widget_show_all(dlg);
    char *res = NULL;
    for (;;) {
        if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_OK) break;
        const char *p1 = gtk_entry_get_text(GTK_ENTRY(e1));
        if (!*p1) continue;
        if (confirm && strcmp(p1, gtk_entry_get_text(GTK_ENTRY(e2)))) { gtk_label_set_text(GTK_LABEL(lab), T(K_MISMATCH)); continue; }
        res = g_strdup(p1); break;
    }
    gtk_entry_set_text(GTK_ENTRY(e1), ""); gtk_entry_set_text(GTK_ENTRY(e2), "");
    gtk_widget_destroy(dlg);
    return res;
}

/* 1 = bóveda abierta. Pide la maestra (o la crea si no existe y se permite) */
static int ensure_vault(gboolean create_if_missing) {
    if (vunlocked) return 1;
    if (!vault_exists()) {
        if (!create_if_missing) return 0;
        char *m = ask_master(T(K_CREATE_TITLE), T(K_CREATE_MSG), TRUE);
        if (!m) return 0;
        int rc = vault_create(m); memset(m, 0, strlen(m)); g_free(m);
        return rc == 0;
    }
    const char *msg = T(K_UNLOCK_MSG);
    for (int tries = 0; tries < 3; tries++) {
        char *m = ask_master(T(K_UNLOCK_TITLE), msg, FALSE);
        if (!m) return 0;
        int rc = vault_unlock(m); memset(m, 0, strlen(m)); g_free(m);
        if (rc == 0) { vault_declined = 0; return 1; }
        msg = T(K_WRONG_PW);
    }
    return 0;
}

static char *origin_of(const char *uri) {
    if (!uri) return NULL;
    int sl = g_str_has_prefix(uri, "https://") ? 8 : g_str_has_prefix(uri, "http://") ? 7 : 0;
    if (!sl) return NULL;
    size_t n = strcspn(uri + sl, "/?#");
    return g_strndup(uri, sl + n);
}
static char *js_quote(const char *s) {
    GString *g = g_string_new("\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') g_string_append_printf(g, "\\%c", *p);
        else if (*p < 0x20 || *p == '<' || *p == '>' || *p == '&') g_string_append_printf(g, "\\u%04x", *p);
        else g_string_append_c(g, (char)*p);
    }
    g_string_append_c(g, '"');
    return g_string_free(g, FALSE);
}

static const char *PW_SCRIPT =
"(function(){"
" if(window.__cosmosPw) return; window.__cosmosPw=1;"
" function send(m){try{window.webkit.messageHandlers.cosmos.postMessage(m);}catch(e){}}"
" function vis(e){return e&&e.offsetWidth>0&&e.offsetHeight>0;}"
" function userField(p){var all=Array.prototype.slice.call(document.querySelectorAll('input'));var i=all.indexOf(p),best=null;"
"  for(var k=i-1;k>=0;k--){var t=(all[k].type||'text').toLowerCase();if((t=='text'||t=='email'||t=='tel')&&vis(all[k])){best=all[k];break;}}return best;}"
" function collect(scope){var p=scope.querySelector('input[type=password]');if(!p||!p.value)return;"
"  var u=userField(p);send('save\\t'+(u?u.value:'')+'\\t'+p.value);}"
" document.addEventListener('submit',function(e){collect(e.target);},true);"
" document.addEventListener('click',function(e){var b=e.target.closest&&e.target.closest('button,input[type=submit],[role=button]');"
"  if(!b)return;var f=b.form||b.closest('form')||document;collect(f);},true);"
" document.addEventListener('keydown',function(e){if(e.key==='Enter'&&e.target&&e.target.tagName==='INPUT'){var f=e.target.form||document;collect(f);}},true);"
" function setv(el,v){var d=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');d.set.call(el,v);"
"  el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));}"
" window.__cosmosFill=function(u,pw){var ps=document.querySelectorAll('input[type=password]');"
"  for(var i=0;i<ps.length;i++){if(!vis(ps[i]))continue;var uf=userField(ps[i]);if(uf&&u&&!uf.value)setv(uf,u);if(!ps[i].value)setv(ps[i],pw);return;}};"
" function ask(){var ps=document.querySelectorAll('input[type=password]');for(var i=0;i<ps.length;i++)if(vis(ps[i])){send('need');return true;}return false;}"
" var tries=0;function loop(){if(ask()||++tries>8)return;setTimeout(loop,1000);}"
" if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',loop);else loop();"
"})();";

static void fill_view(WebKitWebView *v, Cred *c) {
    char *u = js_quote(c->user), *p = js_quote(c->pass);
    char *js = g_strdup_printf("window.__cosmosFill&&window.__cosmosFill(%s,%s);", u, p);
    webkit_web_view_evaluate_javascript(v, js, -1, NULL, NULL, NULL, NULL, NULL);
    memset(js, 0, strlen(js)); memset(p, 0, strlen(p)); g_free(js); g_free(u); g_free(p);
}

static int pw_busy;
static void handle_msg(WebKitWebView *v, const char *msg) {
    if (is_incog(GTK_WIDGET(v)) || pw_busy) return;
    char *origin = origin_of(webkit_web_view_get_uri(v));     /* el origen sale del navegador, nunca de la página */
    if (!origin) return;
    char **f = g_strsplit(msg, "\t", 3);
    pw_busy = 1;
    if (!strcmp(f[0], "need")) {
        if (!g_object_get_data(G_OBJECT(v), "asked")) {
            g_object_set_data(G_OBJECT(v), "asked", GINT_TO_POINTER(1));
            if (vunlocked || (vault_exists() && !vault_declined)) {
                if (!vunlocked && !ensure_vault(FALSE)) vault_declined = 1;
                if (vunlocked) { Cred *c = cred_find(origin, NULL); if (c) fill_view(v, c); }
            }
        }
    } else if (!strcmp(f[0], "save") && f[1] && f[2] && *f[2]) {
        Cred *ex = vunlocked ? cred_find(origin, f[1]) : NULL;
        if (!(ex && !strcmp(ex->pass, f[2]))) {
            char *q = g_strdup_printf(T(K_SAVE_Q), *f[1] ? f[1] : T(K_NO_USER), origin);
            GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", q);
            gtk_dialog_add_buttons(GTK_DIALOG(d), T(K_NOT_NOW), GTK_RESPONSE_NO, ex ? T(K_UPDATE) : T(K_SAVE), GTK_RESPONSE_YES, NULL);
            int r = getenv("COSMOS_TEST_AUTOYES") ? GTK_RESPONSE_YES : gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d); g_free(q);
            if (r == GTK_RESPONSE_YES && ensure_vault(TRUE)) cred_put(origin, f[1], f[2]);
        }
    }
    pw_busy = 0;
    g_strfreev(f); g_free(origin);
}
static void on_script_msg(WebKitUserContentManager *m, WebKitJavascriptResult *jr, gpointer view) {
    char *s = jsc_value_to_string(webkit_javascript_result_get_js_value(jr));
    if (s) { handle_msg(WEBKIT_WEB_VIEW(view), s); memset(s, 0, strlen(s)); g_free(s); }
}

/* ---------- historial / autocompletado ---------- */
static void comp_add(const char *url, const char *title) {
    GtkTreeIter it;
    char *key = g_strdup_printf("%s %s", title ? title : "", url);
    gtk_list_store_append(comp_store, &it);
    gtk_list_store_set(comp_store, &it, 0, url, 1, key, 2, title ? title : "", -1);
    g_free(key);
}
static gboolean comp_match(GtkEntryCompletion *c, const char *typed, GtkTreeIter *it, gpointer d) {
    char *key = NULL; gtk_tree_model_get(GTK_TREE_MODEL(comp_store), it, 1, &key, -1);
    gboolean ok = key && *typed && strcasestr(key, typed) != NULL;
    g_free(key);
    return ok;
}
static void comp_fill(void) {
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < bms->len; i++) { BEntry *e = g_ptr_array_index(bms, i); if (!g_hash_table_contains(seen, e->url)) { g_hash_table_add(seen, e->url); comp_add(e->url, e->title); } }
    for (int i = (int)hist->len - 1; i >= 0 && i > (int)hist->len - 3000; i--) { HEntry *e = g_ptr_array_index(hist, i); if (!g_hash_table_contains(seen, e->url)) { g_hash_table_add(seen, e->url); comp_add(e->url, e->title); } }
    g_hash_table_destroy(seen);
}

static void on_load(WebKitWebView *v, WebKitLoadEvent e, gpointer d) {
    if (e == WEBKIT_LOAD_STARTED) g_object_set_data(G_OBJECT(v), "asked", NULL);
    if (e == WEBKIT_LOAD_FINISHED && !is_incog(GTK_WIDGET(v))) {
        const char *u = webkit_web_view_get_uri(v);
        if (u && (g_str_has_prefix(u, "http://") || g_str_has_prefix(u, "https://"))) {
            int known = 0; for (guint i = 0; i < hist->len && !known; i++) known = !strcmp(((HEntry *)g_ptr_array_index(hist, i))->url, u);
            hist_add(u, webkit_web_view_get_title(v));
            if (!known) comp_add(u, webkit_web_view_get_title(v));
        }
    }
    if (v == cur_view()) sync_chrome();
}
static gboolean on_perm(WebKitWebView *v, WebKitPermissionRequest *r, gpointer d) { webkit_permission_request_deny(r); return TRUE; }

static GtkWidget *new_tab_ex(WebKitWebView *related, const char *url, gboolean focus, gboolean incog);
static GtkWidget *on_create(WebKitWebView *v, WebKitNavigationAction *a, gpointer d) { return new_tab_ex(v, NULL, TRUE, is_incog(GTK_WIDGET(v))); }
#define new_tab(r, u, f) new_tab_ex((r), (u), (f), FALSE)

static void close_tab_for(GtkWidget *view) {
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)) <= 1) { gtk_widget_destroy(win); return; }
    gtk_notebook_remove_page(GTK_NOTEBOOK(nb), gtk_notebook_page_num(GTK_NOTEBOOK(nb), view));
    /* al cerrar la última pestaña privada se descarta toda su sesión */
    if (ictx) {
        int left = 0;
        for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)); i++) left += is_incog(gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), i));
        if (!left) g_clear_object(&ictx);
    }
}
static void on_close_btn(GtkButton *b, gpointer view) { close_tab_for(GTK_WIDGET(view)); }
static void on_close_req(WebKitWebView *v, gpointer d) { close_tab_for(GTK_WIDGET(v)); }

static void on_download(WebKitWebContext *c, WebKitDownload *dl, gpointer d);
static void setup_ctx(WebKitWebContext *c) {
    webkit_web_context_register_uri_scheme(c, "cosmos", scheme_cb, NULL, NULL);
    webkit_security_manager_register_uri_scheme_as_secure(webkit_web_context_get_security_manager(c), "cosmos");
    g_signal_connect(c, "download-started", G_CALLBACK(on_download), NULL);
}

static GtkWidget *new_tab_ex(WebKitWebView *related, const char *url, gboolean focus, gboolean incog) {
    WebKitUserContentManager *ucm = webkit_user_content_manager_new();
    if (!incog) {      /* en privada no se guardan ni se rellenan contraseñas */
        WebKitUserScript *us = webkit_user_script_new(PW_SCRIPT, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
        webkit_user_content_manager_add_script(ucm, us); webkit_user_script_unref(us);
        webkit_user_content_manager_register_script_message_handler(ucm, "cosmos");
    }
    WebKitWebContext *c = incog ? (ictx ? ictx : (ictx = webkit_web_context_new_ephemeral(), setup_ctx(ictx), ictx)) : ctx;
    GtkWidget *view = related
        ? g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related, "user-content-manager", ucm, "settings", settings, NULL)
        : g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", c, "user-content-manager", ucm, "settings", settings, NULL);
    g_object_set_data(G_OBJECT(view), "incog", GINT_TO_POINTER(incog ? 1 : 0));
    if (!incog) g_signal_connect(ucm, "script-message-received::cosmos", G_CALLBACK(on_script_msg), view);
    g_object_unref(ucm);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *lab = gtk_label_new(incog ? T(K_PRIVATE) : T(K_NEW_TAB));
    gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(lab), 16); gtk_label_set_max_width_chars(GTK_LABEL(lab), 22);
    if (incog) { GtkWidget *i = gtk_label_new("\xf0\x9f\x95\xb6"); gtk_box_pack_start(GTK_BOX(box), i, FALSE, FALSE, 0); gtk_style_context_add_class(gtk_widget_get_style_context(box), "incog"); }
    GtkWidget *x = gtk_button_new_with_label("\xc3\x97");
    gtk_style_context_add_class(gtk_widget_get_style_context(x), "tabclose");
    gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(box), x, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    g_object_set_data(G_OBJECT(view), "label", lab);
    g_signal_connect(x, "clicked", G_CALLBACK(on_close_btn), view);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_title), NULL);
    g_signal_connect(view, "notify::uri", G_CALLBACK(on_any), NULL);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_any), NULL);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load), NULL);
    g_signal_connect(view, "mouse-target-changed", G_CALLBACK(on_hover), NULL);
    g_signal_connect(view, "create", G_CALLBACK(on_create), NULL);
    g_signal_connect(view, "close", G_CALLBACK(on_close_req), NULL);
    g_signal_connect(view, "permission-request", G_CALLBACK(on_perm), NULL);
    int pg = gtk_notebook_append_page(GTK_NOTEBOOK(nb), view, box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(nb), view, TRUE);
    gtk_widget_show(view);
    if (focus) gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), pg);
    if (url) { char *u = omni(url); webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view), u); g_free(u); }
    return view;
}

/* ---------- acciones ---------- */
static void go_entry(void) {
    WebKitWebView *v = cur_view(); if (!v) return;
    char *u = omni(gtk_entry_get_text(GTK_ENTRY(entry)));
    webkit_web_view_load_uri(v, u); g_free(u);
    gtk_widget_grab_focus(GTK_WIDGET(v));
}
static void act_back(void) { WebKitWebView *v = cur_view(); if (v) webkit_web_view_go_back(v); }
static void act_fwd(void) { WebKitWebView *v = cur_view(); if (v) webkit_web_view_go_forward(v); }
static void act_reload(void) { WebKitWebView *v = cur_view(); if (!v) return; if (webkit_web_view_is_loading(v)) webkit_web_view_stop_loading(v); else webkit_web_view_reload(v); }
static void act_home(void) { WebKitWebView *v = cur_view(); if (v) webkit_web_view_load_uri(v, HOME_URL); }
static void zoom(double d) { WebKitWebView *v = cur_view(); if (!v) return; double z = d == 0 ? 1.0 : webkit_web_view_get_zoom_level(v) + d; if (z < 0.3) z = 0.3; if (z > 4) z = 4; webkit_web_view_set_zoom_level(v, z); }
static void on_new_btn(GtkButton *b, gpointer d) { new_tab(NULL, HOME_URL, TRUE); gtk_widget_grab_focus(entry); }
static void act_bookmark(void) {
    WebKitWebView *v = cur_view(); if (!v) return;
    const char *u = webkit_web_view_get_uri(v); if (!u || g_str_has_prefix(u, "cosmos://")) return;
    if (bm_find(u) >= 0) { bm_remove(u); gtk_label_set_text(GTK_LABEL(status), T(K_BM_REMOVED)); }
    else { bm_add(u, webkit_web_view_get_title(v)); comp_add(u, webkit_web_view_get_title(v)); gtk_label_set_text(GTK_LABEL(status), T(K_BM_ADDED)); }
    sync_chrome();
}
static void open_internal(const char *url) { new_tab(NULL, url, TRUE); }
static void act_private(void) { new_tab_ex(NULL, HOME_URL, TRUE, TRUE); gtk_widget_grab_focus(entry); }
static void act_passwords(void) {
    if (!vunlocked) { vault_declined = 0; ensure_vault(TRUE); }
    WebKitWebView *v = cur_view();
    const char *u = v ? webkit_web_view_get_uri(v) : NULL;
    if (u && g_str_has_prefix(u, "cosmos://passwords")) webkit_web_view_reload(v); else open_internal("cosmos://passwords");
}
static void act_lock(void) { vault_lock(); gtk_label_set_text(GTK_LABEL(status), T(K_PW_LOCKED_STATUS)); }

/* ---------- importar desde otro navegador ---------- */
static void info_box(const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}
static char *import_run(const char *f) {
    char *base = g_path_get_basename(f), *res;
    WebKitCookieManager *cm = webkit_web_context_get_cookie_manager(ctx);
    if (g_str_has_suffix(base, ".sqlite")) {
        int n = import_cookies_ff(f, cm);
        res = n < 0 ? g_strdup(T(K_IMP_FF_FAIL)) : g_strdup_printf(T(K_IMP_COOK_OK), n);
    } else if (g_str_has_suffix(base, ".csv")) {
        if (!ensure_vault(TRUE)) res = g_strdup(T(K_IMP_NEED_MASTER));
        else {
            int sk, n = import_passwords_csv(f, origin_of, &sk);
            res = n == -2 ? g_strdup(T(K_IMP_CSV_BAD)) : n < 0 ? g_strdup(T(K_IMP_READ_FAIL)) : g_strdup_printf(T(K_IMP_PW_OK), n, sk);
        }
    } else {
        int n = import_cookies_txt(f, cm);
        res = n < 0 ? g_strdup(T(K_IMP_READ_FAIL)) : n == 0 ? g_strdup(T(K_IMP_TXT_NONE)) : g_strdup_printf(T(K_IMP_COOK_OK), n);
    }
    g_free(base); return res;
}
static gboolean act_downloads_once(gpointer d) { act_downloads(); return FALSE; }
static gboolean test_import(gpointer d) {
    gchar **fs = g_strsplit(getenv("COSMOS_TEST_IMPORT"), ":", -1);
    for (int i = 0; fs[i]; i++) { char *r = import_run(fs[i]); g_printerr("IMPORT %s -> %s\n", fs[i], r); g_free(r); }
    g_strfreev(fs); return FALSE;
}
static void act_import(void) {
    GtkWidget *d = gtk_file_chooser_dialog_new(T(K_IMP_TITLE),
        GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN, T(K_CANCEL), GTK_RESPONSE_CANCEL, T(K_IMPORT), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        gtk_widget_destroy(d);
        char *r = import_run(f); info_box(r); g_free(r); g_free(f);
    } else gtk_widget_destroy(d);
}
static void on_m_new(void) { new_tab(NULL, HOME_URL, TRUE); gtk_widget_grab_focus(entry); }
static void on_m_hist(void) { open_internal("cosmos://history"); }
static void on_m_bm(void) { open_internal("cosmos://bookmarks"); }
static void on_m_about(void) { open_internal("cosmos://about"); }
static void menu_add(GtkWidget *m, const char *label, const char *shortcut, GCallback cb) {
    char *l = shortcut ? g_strdup_printf("%s   (%s)", label, shortcut) : g_strdup(label);
    GtkWidget *mi = gtk_menu_item_new_with_label(l); g_free(l);
    g_signal_connect(mi, "activate", cb, NULL); gtk_menu_shell_append(GTK_MENU_SHELL(m), mi);
}
static void on_menu_btn(GtkWidget *b, gpointer d) {
    GtkWidget *m = gtk_menu_new();
    menu_add(m, T(K_NEW_TAB), "Ctrl+T", G_CALLBACK(on_m_new));
    menu_add(m, T(K_M_PRIVATE), "Ctrl+Shift+N", G_CALLBACK(act_private));
    menu_add(m, T(K_M_HISTORY), "Ctrl+H", G_CALLBACK(on_m_hist));
    menu_add(m, T(K_M_BOOKMARKS), "Ctrl+Shift+O", G_CALLBACK(on_m_bm));
    menu_add(m, T(K_M_DOWNLOADS), "Ctrl+J", G_CALLBACK(act_downloads));
    menu_add(m, T(K_M_PASSWORDS), "Ctrl+Shift+P", G_CALLBACK(act_passwords));
    menu_add(m, T(K_M_LOCK), NULL, G_CALLBACK(act_lock));
    menu_add(m, T(K_M_IMPORT), NULL, G_CALLBACK(act_import));
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    GtkWidget *lm = gtk_menu_item_new_with_label(T(K_M_LANGUAGE)), *sub = gtk_menu_new(); GSList *grp = NULL;
    for (int i = 0; i < LANG_N; i++) {
        GtkWidget *it = gtk_radio_menu_item_new_with_label(grp, LANG_NAME[i]); grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), i == lang);
        g_signal_connect(it, "toggled", G_CALLBACK(on_lang_item), GINT_TO_POINTER(i)); gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(lm), sub); gtk_menu_shell_append(GTK_MENU_SHELL(m), lm);
    menu_add(m, T(K_M_ABOUT), NULL, G_CALLBACK(on_m_about));
    gtk_widget_show_all(m);
    gtk_menu_popup_at_widget(GTK_MENU(m), b, GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}
static void on_switch(GtkNotebook *n, GtkWidget *p, guint i, gpointer d) { sync_chrome(); }
static void on_tab_removed(GtkNotebook *n, GtkWidget *p, guint i, gpointer d) { sync_chrome(); }

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
    guint k = e->keyval; gboolean ctrl = e->state & GDK_CONTROL_MASK, alt = e->state & GDK_MOD1_MASK;
    gboolean shift = e->state & GDK_SHIFT_MASK;
    if (ctrl && shift && (k == GDK_KEY_N || k == GDK_KEY_n)) { act_private(); return TRUE; }
    if (ctrl && shift && (k == GDK_KEY_P || k == GDK_KEY_p)) { act_passwords(); return TRUE; }
    if (ctrl && shift && (k == GDK_KEY_O || k == GDK_KEY_o)) { on_m_bm(); return TRUE; }
    if (ctrl && !shift && (k == GDK_KEY_h)) { on_m_hist(); return TRUE; }
    if (ctrl && !shift && k == GDK_KEY_d) { act_bookmark(); return TRUE; }
    if (ctrl && !shift && k == GDK_KEY_j) { act_downloads(); return TRUE; }
    if (ctrl && k == GDK_KEY_t) { GtkWidget *v = new_tab(NULL, HOME_URL, TRUE); (void)v; gtk_widget_grab_focus(entry); return TRUE; }
    if (ctrl && k == GDK_KEY_w) { WebKitWebView *v = cur_view(); if (v) close_tab_for(GTK_WIDGET(v)); return TRUE; }
    if ((ctrl && k == GDK_KEY_l) || k == GDK_KEY_F6) { gtk_widget_grab_focus(entry); gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1); return TRUE; }
    if (k == GDK_KEY_F5 || (ctrl && k == GDK_KEY_r)) { WebKitWebView *v = cur_view(); if (v) webkit_web_view_reload(v); return TRUE; }
    if (ctrl && k == GDK_KEY_F5) { WebKitWebView *v = cur_view(); if (v) webkit_web_view_reload_bypass_cache(v); return TRUE; }
    if (alt && k == GDK_KEY_Left) { act_back(); return TRUE; }
    if (alt && k == GDK_KEY_Right) { act_fwd(); return TRUE; }
    if (ctrl && (k == GDK_KEY_plus || k == GDK_KEY_equal || k == GDK_KEY_KP_Add)) { zoom(0.1); return TRUE; }
    if (ctrl && (k == GDK_KEY_minus || k == GDK_KEY_KP_Subtract)) { zoom(-0.1); return TRUE; }
    if (ctrl && k == GDK_KEY_0) { zoom(0); return TRUE; }
    if (ctrl && k == GDK_KEY_Tab) { gtk_notebook_next_page(GTK_NOTEBOOK(nb)); return TRUE; }
    if (ctrl && k == GDK_KEY_ISO_Left_Tab) { gtk_notebook_prev_page(GTK_NOTEBOOK(nb)); return TRUE; }
    if (k == GDK_KEY_F11) { fullscreen_on = !fullscreen_on; if (fullscreen_on) gtk_window_fullscreen(GTK_WINDOW(win)); else gtk_window_unfullscreen(GTK_WINDOW(win)); return TRUE; }
    if (k == GDK_KEY_F12 || (ctrl && e->state & GDK_SHIFT_MASK && k == GDK_KEY_I)) {
        WebKitWebView *v = cur_view(); if (v) webkit_web_inspector_show(webkit_web_view_get_inspector(v)); return TRUE;
    }
    if (ctrl && k >= GDK_KEY_1 && k <= GDK_KEY_8) { gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), k - GDK_KEY_1); return TRUE; }
    return FALSE;
}

/* ---------- descargas: carpeta Download, círculo de progreso y ventanita de historial ---------- */
typedef struct { WebKitDownload *wk; char *name, *path, *url; int state; double prog; gint64 t; int incog; } DL;   /* state 0 en curso, 1 lista, 2 falló, 3 cancelada */
static GPtrArray *dls;
static GtkWidget *btn_dl, *dl_pop, *dl_box;
static int dl_flash;
static guint dl_flash_id, dl_rebuild_id;

static char *dl_dir(void) {
    char *d = g_build_filename(g_get_home_dir(), "Download", NULL);
    if (!g_file_test(d, G_FILE_TEST_IS_DIR)) {
        const char *x = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        if (x && g_file_test(x, G_FILE_TEST_IS_DIR) && strcmp(x, g_get_home_dir())) { g_free(d); d = g_strdup(x); }
    }
    g_mkdir_with_parents(d, 0755);
    return d;
}
static char *unique_path(const char *dir, const char *suggested) {
    char *name = g_path_get_basename(suggested && *suggested ? suggested : "download");
    if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, "/")) { g_free(name); name = g_strdup("download"); }
    char *path = g_build_filename(dir, name, NULL);
    for (int n = 1; g_file_test(path, G_FILE_TEST_EXISTS) && n < 10000; n++) {
        char *dot = strrchr(name, '.'), *nn;
        if (dot && dot != name) { char *stem = g_strndup(name, dot - name); nn = g_strdup_printf("%s (%d)%s", stem, n, dot); g_free(stem); }
        else nn = g_strdup_printf("%s (%d)", name, n);
        g_free(path); path = g_build_filename(dir, nn, NULL); g_free(nn);
    }
    g_free(name);
    return path;
}
static void dl_free(gpointer p) { DL *d = p; g_free(d->name); g_free(d->path); g_free(d->url); g_free(d); }
static void dl_save(void) {
    char *p = data_path("downloads.tsv"); GString *g = g_string_new(NULL);
    guint from = dls->len > 300 ? dls->len - 300 : 0;
    for (guint i = from; i < dls->len; i++) {
        DL *d = g_ptr_array_index(dls, i);
        if (d->incog || d->state == 0) continue;
        char *a = clean_field(d->name), *b = clean_field(d->path), *c = clean_field(d->url);
        g_string_append_printf(g, "%" G_GINT64_FORMAT "\t%s\t%s\t%s\t%d\n", d->t, a, b, c, d->state);
        g_free(a); g_free(b); g_free(c);
    }
    g_file_set_contents(p, g->str, g->len, NULL); g_chmod(p, 0600);
    g_string_free(g, TRUE); g_free(p);
}
static void dl_load(void) {
    dls = g_ptr_array_new_with_free_func(dl_free);
    char *p = data_path("downloads.tsv"), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **ln = g_strsplit(txt, "\n", -1);
        for (int i = 0; ln[i]; i++) {
            char **f = g_strsplit(ln[i], "\t", 5);
            if (g_strv_length(f) == 5) {
                DL *d = g_new0(DL, 1); d->t = g_ascii_strtoll(f[0], NULL, 10); d->name = g_strdup(f[1]); d->path = g_strdup(f[2]);
                d->url = g_strdup(f[3]); d->state = atoi(f[4]); d->prog = d->state == 1 ? 1 : 0; g_ptr_array_add(dls, d);
            }
            g_strfreev(f);
        }
        g_strfreev(ln); g_free(txt);
    }
    g_free(p);
}
static void dl_open_uri(const char *path, gboolean parent) {
    char *target = parent ? g_path_get_dirname(path) : g_strdup(path);
    char *uri = g_filename_to_uri(target, NULL, NULL);
    if (uri) g_app_info_launch_default_for_uri(uri, NULL, NULL);
    g_free(uri); g_free(target);
}
static void on_dl_row(GtkButton *b, gpointer data) {
    DL *d = data;
    if (d->state == 1 && d->path && g_file_test(d->path, G_FILE_TEST_EXISTS)) dl_open_uri(d->path, FALSE);
    else if (d->state == 1) gtk_label_set_text(GTK_LABEL(status), T(K_DL_MISSING));
}
static void on_dl_cancel(GtkButton *b, gpointer data) { DL *d = data; if (d->wk) webkit_download_cancel(d->wk); }
static void on_dl_folder(GtkButton *b, gpointer data) { char *dir = dl_dir(), *x = g_build_filename(dir, "x", NULL); dl_open_uri(x, TRUE); g_free(dir); g_free(x); }
static void dl_rebuild(void);
static void on_dl_clear(GtkButton *b, gpointer data) {
    for (int i = (int)dls->len - 1; i >= 0; i--) if (((DL *)g_ptr_array_index(dls, i))->state != 0) g_ptr_array_remove_index(dls, i);
    dl_save(); dl_rebuild();
    if (!dls->len && dl_pop) gtk_popover_popdown(GTK_POPOVER(dl_pop));
    gtk_widget_set_visible(btn_dl, dls->len > 0); gtk_widget_queue_draw(btn_dl);
}
static void dl_rebuild(void) {
    if (!dl_pop) return;
    GList *kids = gtk_container_get_children(GTK_CONTAINER(dl_box));
    for (GList *k = kids; k; k = k->next) gtk_widget_destroy(k->data);
    g_list_free(kids);
    if (!dls->len) { GtkWidget *l = gtk_label_new(T(K_DL_EMPTY)); gtk_widget_set_margin_top(l, 8); gtk_widget_set_margin_bottom(l, 8); gtk_box_pack_start(GTK_BOX(dl_box), l, FALSE, FALSE, 0); }
    for (int i = (int)dls->len - 1; i >= 0 && i > (int)dls->len - 60; i--) {
        DL *d = g_ptr_array_index(dls, i);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *b = gtk_button_new(); gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *nl = gtk_label_new(d->name && *d->name ? d->name : "…"); gtk_label_set_xalign(GTK_LABEL(nl), 0);
        gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_MIDDLE); gtk_label_set_max_width_chars(GTK_LABEL(nl), 34);
        char *st = d->state == 0 ? g_strdup_printf(T(K_DL_PROGRESS), (int)(d->prog * 100))
                 : d->state == 1 ? g_strdup(T(K_DL_DONE)) : d->state == 2 ? g_strdup(T(K_DL_FAILED)) : g_strdup(T(K_DL_CANCELED));
        GtkWidget *sl = gtk_label_new(st); g_free(st); gtk_label_set_xalign(GTK_LABEL(sl), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(sl), "dlsub");
        gtk_box_pack_start(GTK_BOX(v), nl, FALSE, FALSE, 0);
        if (d->state == 0) { GtkWidget *pb = gtk_progress_bar_new(); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb), d->prog); gtk_box_pack_start(GTK_BOX(v), pb, FALSE, FALSE, 2); }
        gtk_box_pack_start(GTK_BOX(v), sl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(b), v);
        g_signal_connect(b, "clicked", G_CALLBACK(on_dl_row), d);
        gtk_box_pack_start(GTK_BOX(row), b, TRUE, TRUE, 0);
        if (d->state == 0) { GtkWidget *x = gtk_button_new_with_label("\xe2\x9c\x95"); gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
            g_signal_connect(x, "clicked", G_CALLBACK(on_dl_cancel), d); gtk_box_pack_start(GTK_BOX(row), x, FALSE, FALSE, 0); }
        gtk_box_pack_start(GTK_BOX(dl_box), row, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(dl_box);
}
static gboolean dl_rebuild_cb(gpointer d) { dl_rebuild_id = 0; if (dl_pop && gtk_widget_get_visible(dl_pop)) dl_rebuild(); return FALSE; }
static void dl_changed(void) {
    gtk_widget_set_visible(btn_dl, dls->len > 0 || (dl_pop && gtk_widget_get_visible(dl_pop)));
    gtk_widget_queue_draw(btn_dl);
    if (dl_pop && gtk_widget_get_visible(dl_pop) && !dl_rebuild_id) dl_rebuild_id = g_timeout_add(250, dl_rebuild_cb, NULL);
}
static void act_downloads(void) {
    if (!dl_pop) {
        dl_pop = gtk_popover_new(btn_dl);
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); gtk_container_set_border_width(GTK_CONTAINER(v), 10);
        GtkWidget *t = gtk_label_new(NULL); char *m = g_markup_printf_escaped("<b>%s</b>", T(K_DL_TITLE)); gtk_label_set_markup(GTK_LABEL(t), m); g_free(m);
        gtk_label_set_xalign(GTK_LABEL(t), 0); g_object_set_data(G_OBJECT(dl_pop), "title", t);
        GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sw), TRUE);
        gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sw), 320); gtk_widget_set_size_request(sw, 360, -1);
        dl_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2); gtk_container_add(GTK_CONTAINER(sw), dl_box);
        GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *bf = gtk_button_new_with_label(T(K_DL_OPEN_FOLDER)), *bc = gtk_button_new_with_label(T(K_DL_CLEAR));
        g_object_set_data(G_OBJECT(dl_pop), "bf", bf); g_object_set_data(G_OBJECT(dl_pop), "bc", bc);
        g_signal_connect(bf, "clicked", G_CALLBACK(on_dl_folder), NULL); g_signal_connect(bc, "clicked", G_CALLBACK(on_dl_clear), NULL);
        gtk_box_pack_start(GTK_BOX(bar), bf, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(bar), bc, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(v), t, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(v), sw, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(v), bar, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(dl_pop), v); gtk_widget_show_all(v);
    } else {     /* textos fijos: se actualizan por si cambió el idioma */
        char *m = g_markup_printf_escaped("<b>%s</b>", T(K_DL_TITLE));
        gtk_label_set_markup(GTK_LABEL(g_object_get_data(G_OBJECT(dl_pop), "title")), m); g_free(m);
        gtk_button_set_label(GTK_BUTTON(g_object_get_data(G_OBJECT(dl_pop), "bf")), T(K_DL_OPEN_FOLDER));
        gtk_button_set_label(GTK_BUTTON(g_object_get_data(G_OBJECT(dl_pop), "bc")), T(K_DL_CLEAR));
    }
    if (gtk_widget_get_visible(dl_pop)) { gtk_popover_popdown(GTK_POPOVER(dl_pop)); return; }
    gtk_widget_show(btn_dl);
    dl_rebuild();
    gtk_popover_popup(GTK_POPOVER(dl_pop));
}
static gboolean dl_flash_end(gpointer d) { dl_flash = 0; dl_flash_id = 0; gtk_widget_queue_draw(btn_dl); return FALSE; }

/* el círculo: gris con relleno azul que crece con el progreso; al terminar se llena entero con una palomita */
static gboolean dl_draw(GtkWidget *w, cairo_t *cr, gpointer d) {
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), cx = W / 2, cy = H / 2, r = (W < H ? W : H) / 2 - 2;
    double sum = 0; int act = 0;
    for (guint i = 0; i < dls->len; i++) { DL *x = g_ptr_array_index(dls, i); if (x->state == 0) { act++; sum += x->prog; } }
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI); cairo_set_source_rgb(cr, 0.10, 0.12, 0.29); cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.17, 0.59, 0.84); cairo_set_line_width(cr, 1.6); cairo_stroke(cr);
    if (act) {
        double p = sum / act; if (p < 0.03) p = 0.03;
        cairo_move_to(cr, cx, cy); cairo_arc(cr, cx, cy, r - 1, -G_PI / 2, -G_PI / 2 + 2 * G_PI * p); cairo_close_path(cr);
        cairo_set_source_rgb(cr, 0.17, 0.59, 0.84); cairo_fill(cr);
    } else if (dl_flash) { cairo_arc(cr, cx, cy, r - 1, 0, 2 * G_PI); cairo_set_source_rgb(cr, 0.17, 0.59, 0.84); cairo_fill(cr); }
    cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_line_width(cr, 2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    if (!act && dl_flash) {      /* palomita */
        cairo_move_to(cr, cx - r * 0.38, cy + r * 0.02); cairo_line_to(cr, cx - r * 0.08, cy + r * 0.32); cairo_line_to(cr, cx + r * 0.42, cy - r * 0.30); cairo_stroke(cr);
    } else {                     /* flecha hacia abajo */
        cairo_move_to(cr, cx, cy - r * 0.40); cairo_line_to(cr, cx, cy + r * 0.34);
        cairo_move_to(cr, cx - r * 0.30, cy + r * 0.06); cairo_line_to(cr, cx, cy + r * 0.38); cairo_line_to(cr, cx + r * 0.30, cy + r * 0.06); cairo_stroke(cr);
    }
    return FALSE;
}

static void on_dl_progress(GObject *o, GParamSpec *p, gpointer data) { DL *d = data; d->prog = webkit_download_get_estimated_progress(d->wk); dl_changed(); }
static gboolean on_dl_decide(WebKitDownload *dl, const char *suggested, gpointer data) {
    DL *d = data; char *dir = dl_dir();
    g_free(d->path); d->path = unique_path(dir, suggested); g_free(d->name); d->name = g_path_get_basename(d->path);
    char *uri = g_filename_to_uri(d->path, NULL, NULL);
    webkit_download_set_destination(dl, uri);
    gtk_label_set_text(GTK_LABEL(status), T(K_DL_STATUS));
    g_free(dir); g_free(uri); dl_changed();
    return TRUE;
}
static void on_dl_failed(WebKitDownload *dl, GError *err, gpointer data) {
    DL *d = data;
    d->state = g_error_matches(err, WEBKIT_DOWNLOAD_ERROR, WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER) ? 3 : 2;
    if (d->path) g_unlink(d->path);                       /* no dejar archivos a medias */
}
static void on_dl_done(WebKitDownload *dl, gpointer data) {
    DL *d = data;
    if (d->state == 0) {
        d->state = 1; d->prog = 1;
        char *m = g_strdup_printf(T(K_DL_FINISHED), d->name ? d->name : ""); gtk_label_set_text(GTK_LABEL(status), m); g_free(m);
        dl_flash = 1; if (dl_flash_id) g_source_remove(dl_flash_id); dl_flash_id = g_timeout_add_seconds(4, dl_flash_end, NULL);
    }
    g_signal_handlers_disconnect_by_data(dl, d);
    g_clear_object(&d->wk);
    dl_save(); dl_changed();
}
static void on_download(WebKitWebContext *c, WebKitDownload *dl, gpointer ud) {
    DL *d = g_new0(DL, 1);
    d->wk = g_object_ref(dl); d->t = time(NULL); d->incog = (c == ictx);
    WebKitURIRequest *rq = webkit_download_get_request(dl); d->url = g_strdup(rq ? webkit_uri_request_get_uri(rq) : "");
    char *b = g_path_get_basename(d->url); d->name = g_uri_unescape_string(b, NULL); g_free(b);
    if (!d->name) d->name = g_strdup("…");
    g_ptr_array_add(dls, d);
    g_signal_connect(dl, "decide-destination", G_CALLBACK(on_dl_decide), d);
    g_signal_connect(dl, "notify::estimated-progress", G_CALLBACK(on_dl_progress), d);
    g_signal_connect(dl, "failed", G_CALLBACK(on_dl_failed), d);
    g_signal_connect(dl, "finished", G_CALLBACK(on_dl_done), d);
    dl_changed();
}

/* ---------- idioma ---------- */
static void nav_glyphs(void) {   /* en árabe (derecha a izquierda) atrás/adelante apuntan al revés */
    gtk_button_set_label(GTK_BUTTON(btn_back), lang_rtl() ? "\xe2\x86\x92" : "\xe2\x86\x90");
    gtk_button_set_label(GTK_BUTTON(btn_fwd), lang_rtl() ? "\xe2\x86\x90" : "\xe2\x86\x92");
}
static void lang_apply(void) {
    nav_glyphs();
    gtk_widget_set_default_direction(lang_rtl() ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), T(K_ADDR));
    for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)); i++) {         /* las páginas cosmos:// se regeneran en el idioma nuevo */
        WebKitWebView *v = WEBKIT_WEB_VIEW(gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), i));
        const char *u = webkit_web_view_get_uri(v);
        if (u && g_str_has_prefix(u, "cosmos://")) webkit_web_view_reload(v);
    }
    if (dl_pop) { dl_rebuild(); }
}
static void on_lang_item(GtkCheckMenuItem *mi, gpointer data) {
    if (!gtk_check_menu_item_get_active(mi)) return;
    int i = GPOINTER_TO_INT(data); if (i == lang) return;
    lang = i; lang_save(); lang_apply();
}

static void apply_css(void) {
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        "window,.cosbar{background:#0b0d1e;color:#dfe3ff}"
        "button{background:#1a1f4a;color:#dfe3ff;border:1px solid #2c3370;border-radius:8px;padding:4px 10px;box-shadow:none;background-image:none}"
        "button:hover{background:#2a3180}button:disabled{color:#555b8a}"
        "button.tabclose{padding:0 6px;border:none;background:transparent}"
        "entry{background:#12163a;color:#fff;border:1px solid #3a4280;border-radius:16px;padding:5px 12px;caret-color:#5de0ff;min-height:22px}"
        "entry progress{background:#5de0ff;min-height:2px}"
        "notebook header{background:#0b0d1e}notebook tab{background:#14183d;color:#aab2f0;padding:4px 8px;border:none}"
        "notebook tab:checked{background:#262d6e;color:#fff;border-top:2px solid #5de0ff}"
        "notebook tab box.incog label{color:#ffb4e6}"
        "label.stat{background:#14183d;color:#9fe8ff;padding:2px 8px;font-size:11px}"
        "button.dlbtn{padding:2px;min-width:30px;min-height:30px;border-radius:15px}"
        "popover,popover>box{background:#12163a;color:#dfe3ff}label.dlsub{color:#8a93d6;font-size:11px}progressbar progress{background:#2b97d6}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void activate(GtkApplication *app, gpointer d) {
    lang_load();
    gtk_widget_set_default_direction(lang_rtl() ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
    win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(win), 1150, 720);
    char *ic = g_build_filename(exedir, "icon.png", NULL);
    gtk_window_set_icon_from_file(GTK_WINDOW(win), ic, NULL); g_free(ic);
    apply_css();
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "cosbar");
    gtk_container_set_border_width(GTK_CONTAINER(bar), 6);
    btn_back = gtk_button_new_with_label("\xe2\x86\x90"); btn_fwd = gtk_button_new_with_label("\xe2\x86\x92");
    btn_reload = gtk_button_new_with_label("\xe2\x9f\xb3");
    GtkWidget *btn_home = gtk_button_new_with_label("\xe2\x8c\x82"), *btn_new = gtk_button_new_with_label("+"), *btn_menu = gtk_button_new_with_label("\xe2\x98\xb0");
    btn_star = gtk_button_new_with_label("\xe2\x98\x86");
    btn_dl = gtk_button_new(); gtk_style_context_add_class(gtk_widget_get_style_context(btn_dl), "dlbtn");
    { GtkWidget *da = gtk_drawing_area_new(); gtk_widget_set_size_request(da, 26, 26); gtk_container_add(GTK_CONTAINER(btn_dl), da);
      g_signal_connect(da, "draw", G_CALLBACK(dl_draw), NULL); gtk_widget_show(da); }
    gtk_widget_set_no_show_all(btn_dl, TRUE);
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), T(K_ADDR));
    gtk_box_pack_start(GTK_BOX(bar), btn_back, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(bar), btn_fwd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), btn_reload, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(bar), btn_home, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), entry, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(bar), btn_star, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), btn_dl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), btn_new, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(bar), btn_menu, FALSE, FALSE, 0);
    nb = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    status = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(status), 0); gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_style_context_add_class(gtk_widget_get_style_context(status), "stat");
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), nb, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);

    /* contexto con datos persistentes (cookies, almacenamiento local) */
    char *base = g_build_filename(g_get_user_data_dir(), "cosmos-browser", NULL);
    char *cache = g_build_filename(g_get_user_cache_dir(), "cosmos-browser", NULL);
    WebKitWebsiteDataManager *dm = webkit_website_data_manager_new("base-data-directory", base, "base-cache-directory", cache, NULL);
    ctx = webkit_web_context_new_with_website_data_manager(dm);
    settings = webkit_settings_new();
    g_object_set(settings, "enable-developer-extras", TRUE, "enable-media-stream", FALSE, "enable-smooth-scrolling", TRUE,
                 "enable-webgl", getenv("COSMOS_SOFTWARE") == NULL, "hardware-acceleration-policy",
                 getenv("COSMOS_SOFTWARE") ? WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER : WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS, NULL);
    char *cookies = g_build_filename(base, "cookies.sqlite", NULL);
    webkit_cookie_manager_set_persistent_storage(webkit_web_context_get_cookie_manager(ctx), cookies, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    g_free(cookies); g_free(base); g_free(cache);
    setup_ctx(ctx);
    /* datos del usuario y autocompletado de la barra */
    { unsigned char r[16]; gcry_randomize(r, 16, GCRY_STRONG_RANDOM); for (int i = 0; i < 16; i++) g_snprintf(tok + i * 2, 3, "%02x", r[i]); }
    hist_load(); bm_load(); dl_load();
    gtk_widget_set_visible(btn_dl, dls->len > 0);
    comp_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    comp = gtk_entry_completion_new();
    gtk_entry_completion_set_model(comp, GTK_TREE_MODEL(comp_store));
    gtk_entry_completion_set_text_column(comp, 0);
    gtk_entry_completion_set_match_func(comp, comp_match, NULL, NULL);
    gtk_entry_completion_set_minimum_key_length(comp, 2);
    gtk_entry_completion_set_popup_single_match(comp, TRUE);
    gtk_entry_set_completion(GTK_ENTRY(entry), comp);
    comp_fill();

    g_signal_connect_swapped(btn_back, "clicked", G_CALLBACK(act_back), NULL);
    g_signal_connect_swapped(btn_fwd, "clicked", G_CALLBACK(act_fwd), NULL);
    g_signal_connect_swapped(btn_reload, "clicked", G_CALLBACK(act_reload), NULL);
    g_signal_connect_swapped(btn_home, "clicked", G_CALLBACK(act_home), NULL);
    g_signal_connect_swapped(entry, "activate", G_CALLBACK(go_entry), NULL);
    g_signal_connect(btn_new, "clicked", G_CALLBACK(on_new_btn), NULL);
    g_signal_connect_swapped(btn_dl, "clicked", G_CALLBACK(act_downloads), NULL);
    g_signal_connect_swapped(btn_star, "clicked", G_CALLBACK(act_bookmark), NULL);
    g_signal_connect(btn_menu, "clicked", G_CALLBACK(on_menu_btn), NULL);
    g_signal_connect(nb, "switch-page", G_CALLBACK(on_switch), NULL);
    g_signal_connect(nb, "page-removed", G_CALLBACK(on_tab_removed), NULL);
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
    if (getenv("COSMOS_TEST_IMPORT")) g_timeout_add(1500, test_import, NULL);
    if (getenv("COSMOS_TEST_DLPOP")) g_timeout_add(atoi(getenv("COSMOS_TEST_DLPOP")), (GSourceFunc)(void *)act_downloads_once, NULL);
    gtk_widget_show_all(win);
    nav_glyphs();

    char **files = d;
    int n = 0;
    for (; files && files[n]; n++) new_tab(NULL, files[n], n == 0);
    if (!n) new_tab(NULL, HOME_URL, TRUE);
}

int main(int argc, char **argv) {
    crypto_init();
    ssize_t n = readlink("/proc/self/exe", exedir, sizeof exedir - 1);
    if (n > 0) { exedir[n] = 0; char *s = strrchr(exedir, '/'); if (s) *s = 0; }
    /* GPU por defecto; COSMOS_SOFTWARE=1 fuerza render por software */
    if (getenv("COSMOS_SOFTWARE")) { g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", FALSE); g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", FALSE); }
    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("Cosmos Browser " VERSION "\nuso: cosmos-wk [URL|b\xc3\xbasqueda ...]\n"); return 0;
    }
    char **urls = g_new0(char *, argc + 1);
    for (int i = 1; i < argc; i++) urls[i - 1] = argv[i];
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), urls);
    int rc = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    return rc;
}
