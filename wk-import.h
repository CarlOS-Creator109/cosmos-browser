/* wk-import.h — importar contraseñas (CSV) y cookies (cookies.txt / Firefox cookies.sqlite) */
#ifndef WK_IMPORT_H
#define WK_IMPORT_H
#include <sqlite3.h>

/* ---- CSV (RFC 4180 básico: comillas, comas y saltos de línea dentro de campos) ---- */
static GPtrArray *csv_parse(const char *s) {
    GPtrArray *rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
    GPtrArray *row = g_ptr_array_new(); GString *f = g_string_new(NULL);
    int inq = 0, any = 0;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    for (const char *p = s;; p++) {
        char c = *p;
        if (inq) {
            if (c == '"') { if (p[1] == '"') { g_string_append_c(f, '"'); p++; } else inq = 0; }
            else if (!c) break; else g_string_append_c(f, c);
            continue;
        }
        if (c == '"') { inq = 1; any = 1; }
        else if (c == ',' ) { g_ptr_array_add(row, g_string_free(f, FALSE)); f = g_string_new(NULL); any = 1; }
        else if (c == '\r') { }
        else if (c == '\n' || !c) {
            if (any || f->len) {
                g_ptr_array_add(row, g_string_free(f, FALSE)); f = g_string_new(NULL);
                g_ptr_array_add(row, NULL); g_ptr_array_add(rows, g_ptr_array_free(row, FALSE)); row = g_ptr_array_new();
            }
            any = 0;
            if (!c) break;
        } else { g_string_append_c(f, c); any = 1; }
    }
    g_string_free(f, TRUE); g_ptr_array_free(row, TRUE);
    return rows;
}
static int csv_col(char **hdr, const char *a, const char *b, const char *c) {
    for (int i = 0; hdr[i]; i++) {
        if (!g_ascii_strcasecmp(hdr[i], a) || (b && !g_ascii_strcasecmp(hdr[i], b)) || (c && !g_ascii_strcasecmp(hdr[i], c))) return i;
    }
    return -1;
}
static int ncols(char **r) { int n = 0; while (r[n]) n++; return n; }

/* Chrome/Edge/Brave: name,url,username,password · Firefox: url,username,password,... · Bitwarden: login_uri,login_username,login_password.
   origin_fn convierte una URL en origen http(s). Devuelve nº importadas; *skipped = filas ignoradas. */
static int import_passwords_csv(const char *path, char *(*origin_fn)(const char *), int *skipped) {
    gchar *txt = NULL; *skipped = 0;
    if (!g_file_get_contents(path, &txt, NULL, NULL)) return -1;
    GPtrArray *rows = csv_parse(txt);
    memset(txt, 0, strlen(txt)); g_free(txt);
    int n = 0;
    if (rows->len < 2) { g_ptr_array_free(rows, TRUE); return -2; }
    char **h = g_ptr_array_index(rows, 0);
    int ci = csv_col(h, "url", "login_uri", "origin"), cu = csv_col(h, "username", "login_username", "user"), cp = csv_col(h, "password", "login_password", NULL);
    if (ci < 0 || cu < 0 || cp < 0) { g_ptr_array_free(rows, TRUE); return -2; }
    for (guint i = 1; i < rows->len; i++) {
        char **r = g_ptr_array_index(rows, i); int nc = ncols(r);
        if (ci >= nc || cu >= nc || cp >= nc || !*r[cp]) { (*skipped)++; continue; }
        char *o = origin_fn(r[ci]);
        if (!o) { (*skipped)++; continue; }
        if (cred_put(o, r[cu], r[cp]) || cred_find(o, r[cu])) n++;
        g_free(o);
    }
    g_ptr_array_free(rows, TRUE);
    return n;
}

/* ---- cookies ---- */
static void ck_add(WebKitCookieManager *cm, const char *name, const char *val, const char *dom, const char *path,
                   gint64 expiry, gboolean secure, gboolean httponly) {
    if (!name || !*name || !dom || !*dom) return;
    SoupCookie *c = soup_cookie_new(name, val ? val : "", dom, path && *path ? path : "/", -1);
    if (expiry > 0) {
        if (expiry > 100000000000LL) expiry /= 1000;            /* algunos exportan en ms */
        GDateTime *dt = g_date_time_new_from_unix_utc(expiry);
        if (dt) { soup_cookie_set_expires(c, dt); g_date_time_unref(dt); }
    }
    soup_cookie_set_secure(c, secure); soup_cookie_set_http_only(c, httponly);
    webkit_cookie_manager_add_cookie(cm, c, NULL, NULL, NULL);
    soup_cookie_free(c);
}
/* Netscape cookies.txt: dominio \t subdominios \t ruta \t seguro \t expira \t nombre \t valor */
static int import_cookies_txt(const char *path, WebKitCookieManager *cm) {
    gchar *txt = NULL;
    if (!g_file_get_contents(path, &txt, NULL, NULL)) return -1;
    int n = 0; gchar **lines = g_strsplit(txt, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *l = g_strchomp(lines[i]); gboolean ho = FALSE;
        if (g_str_has_prefix(l, "#HttpOnly_")) { ho = TRUE; l += 10; }
        if (!*l || *l == '#') continue;
        gchar **f = g_strsplit(l, "\t", 7);
        if (g_strv_length(f) == 7) {
            ck_add(cm, f[5], f[6], f[0], f[2], g_ascii_strtoll(f[4], NULL, 10), !g_ascii_strcasecmp(f[3], "TRUE"), ho);
            n++;
        }
        g_strfreev(f);
    }
    g_strfreev(lines); g_free(txt);
    return n;
}
/* Firefox cookies.sqlite (sin cifrar). Se copia a un temporal por si Firefox lo tiene abierto. */
static int import_cookies_ff(const char *path, WebKitCookieManager *cm) {
    char tmpl[] = "/tmp/cosmos-ck-XXXXXX"; if (!mkdtemp(tmpl)) return -1;
    char *tmp = g_build_filename(tmpl, "c.sqlite", NULL); gchar *data = NULL; gsize len;
    int n = -1;
    if (g_file_get_contents(path, &data, &len, NULL) && g_file_set_contents(tmp, data, len, NULL)) {
        char *wal = g_strconcat(path, "-wal", NULL), *wtmp = g_strconcat(tmp, "-wal", NULL); gchar *wd = NULL; gsize wl;
        if (g_file_get_contents(wal, &wd, &wl, NULL)) { g_file_set_contents(wtmp, wd, wl, NULL); g_free(wd); }
        g_free(wal); g_free(wtmp);
        sqlite3 *db;
        if (sqlite3_open_v2(tmp, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK) {   /* RW para poder aplicar el WAL */
            sqlite3_stmt *st;
            if (sqlite3_prepare_v2(db, "SELECT name,value,host,path,expiry,isSecure,isHttpOnly FROM moz_cookies", -1, &st, NULL) == SQLITE_OK) {
                n = 0;
                while (sqlite3_step(st) == SQLITE_ROW) {
                    ck_add(cm, (const char *)sqlite3_column_text(st, 0), (const char *)sqlite3_column_text(st, 1),
                           (const char *)sqlite3_column_text(st, 2), (const char *)sqlite3_column_text(st, 3),
                           sqlite3_column_int64(st, 4), sqlite3_column_int(st, 5), sqlite3_column_int(st, 6));
                    n++;
                }
                sqlite3_finalize(st);
            }
            sqlite3_close(db);
        }
    }
    g_free(data);
    char *w1 = g_strconcat(tmp, "-wal", NULL), *w2 = g_strconcat(tmp, "-shm", NULL);
    g_unlink(tmp); g_unlink(w1); g_unlink(w2); g_rmdir(tmpl); g_free(tmp); g_free(w1); g_free(w2);
    return n;
}
#endif
