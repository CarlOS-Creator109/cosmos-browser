/* Cosmos Browser (WebKit) - datos del usuario: historial, marcadores y bóveda de contraseñas.
 * Solo depende de GLib y libgcrypt (ambas ya vienen con GTK/WebKitGTK). */
#ifndef WK_DATA_H
#define WK_DATA_H
#include <glib.h>
#include <glib/gstdio.h>
#include <gcrypt.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static char *data_dir_path;
static char *data_path(const char *name) {
    if (!data_dir_path) {
        data_dir_path = g_build_filename(g_get_user_data_dir(), "cosmos-browser", NULL);
        g_mkdir_with_parents(data_dir_path, 0700);
    }
    return g_build_filename(data_dir_path, name, NULL);
}

/* los campos van en archivos TSV: sin tabuladores ni saltos de línea */
static char *clean_field(const char *s) {
    char *r = g_strdup(s ? s : "");
    for (char *p = r; *p; p++) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
    return r;
}

/* ---------- historial ---------- */
typedef struct { gint64 t; char *url, *title; } HEntry;
static GPtrArray *hist;
#define HIST_MAX 20000

static void hist_free_entry(gpointer p) { HEntry *e = p; g_free(e->url); g_free(e->title); g_free(e); }
static void hist_load(void) {
    hist = g_ptr_array_new_with_free_func(hist_free_entry);
    char *p = data_path("history.tsv"), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **ls = g_strsplit(txt, "\n", -1);
        for (int i = 0; ls[i]; i++) {
            char **f = g_strsplit(ls[i], "\t", 3);
            if (f[0] && f[1] && *f[1]) {
                HEntry *e = g_new0(HEntry, 1);
                e->t = g_ascii_strtoll(f[0], NULL, 10); e->url = g_strdup(f[1]); e->title = g_strdup(f[2] ? f[2] : "");
                g_ptr_array_add(hist, e);
            }
            g_strfreev(f);
        }
        g_strfreev(ls); g_free(txt);
    }
    g_free(p);
    while (hist->len > HIST_MAX) g_ptr_array_remove_index(hist, 0);
}
static void hist_add(const char *url, const char *title) {
    if (!hist) return;
    if (hist->len) { HEntry *l = g_ptr_array_index(hist, hist->len - 1); if (!strcmp(l->url, url)) { if (title && *title) { g_free(l->title); l->title = clean_field(title); } return; } }
    HEntry *e = g_new0(HEntry, 1);
    e->t = time(NULL); e->url = clean_field(url); e->title = clean_field(title);
    g_ptr_array_add(hist, e);
    char *p = data_path("history.tsv");
    FILE *f = fopen(p, "a");
    if (f) { fprintf(f, "%" G_GINT64_FORMAT "\t%s\t%s\n", e->t, e->url, e->title); fclose(f); g_chmod(p, 0600); }
    g_free(p);
}
static void hist_clear(void) {
    if (hist) g_ptr_array_set_size(hist, 0);
    char *p = data_path("history.tsv"); g_remove(p); g_free(p);
}

/* ---------- marcadores ---------- */
typedef struct { char *url, *title; } BEntry;
static GPtrArray *bms;
static void bm_free_entry(gpointer p) { BEntry *e = p; g_free(e->url); g_free(e->title); g_free(e); }
static void bm_save(void) {
    GString *s = g_string_new("");
    for (guint i = 0; i < bms->len; i++) { BEntry *e = g_ptr_array_index(bms, i); g_string_append_printf(s, "%s\t%s\n", e->url, e->title); }
    char *p = data_path("bookmarks.tsv");
    g_file_set_contents(p, s->str, s->len, NULL); g_chmod(p, 0600);
    g_free(p); g_string_free(s, TRUE);
}
static void bm_load(void) {
    bms = g_ptr_array_new_with_free_func(bm_free_entry);
    char *p = data_path("bookmarks.tsv"), *txt = NULL;
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        char **ls = g_strsplit(txt, "\n", -1);
        for (int i = 0; ls[i]; i++) {
            char **f = g_strsplit(ls[i], "\t", 2);
            if (f[0] && *f[0]) { BEntry *e = g_new0(BEntry, 1); e->url = g_strdup(f[0]); e->title = g_strdup(f[1] ? f[1] : ""); g_ptr_array_add(bms, e); }
            g_strfreev(f);
        }
        g_strfreev(ls); g_free(txt);
    }
    g_free(p);
}
static int bm_find(const char *url) {
    for (guint i = 0; i < bms->len; i++) if (!strcmp(((BEntry *)g_ptr_array_index(bms, i))->url, url)) return (int)i;
    return -1;
}
static void bm_add(const char *url, const char *title) {
    if (bm_find(url) >= 0) return;
    BEntry *e = g_new0(BEntry, 1); e->url = clean_field(url); e->title = clean_field(title && *title ? title : url);
    g_ptr_array_add(bms, e); bm_save();
}
static void bm_remove(const char *url) { int i = bm_find(url); if (i >= 0) { g_ptr_array_remove_index(bms, i); bm_save(); } }

/* ---------- bóveda de contraseñas (AES-256-GCM, clave derivada con scrypt) ---------- */
typedef struct { char *origin, *user, *pass; } Cred;
static GPtrArray *creds;
static unsigned char vkey[32], vsalt[16];
static int vunlocked;

static void cred_free(gpointer p) {
    Cred *c = p; g_free(c->origin); g_free(c->user);
    if (c->pass) { memset(c->pass, 0, strlen(c->pass)); g_free(c->pass); }
    g_free(c);
}
static void crypto_init(void) {
    if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) return;
    gcry_check_version(NULL);
    gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
}
static int vault_exists(void) { char *p = data_path("passwords.vault"); int r = g_file_test(p, G_FILE_TEST_EXISTS); g_free(p); return r; }
static int derive(const char *master, const unsigned char *salt) {
    return gcry_kdf_derive(master, strlen(master), GCRY_KDF_SCRYPT, 32768, salt, 16, 1, 32, vkey) == 0 ? 0 : -1;
}
static char *esc(const char *s) { return g_uri_escape_string(s ? s : "", NULL, TRUE); }

static int vault_save(void) {
    GString *pt = g_string_new("");
    for (guint i = 0; i < creds->len; i++) {
        Cred *c = g_ptr_array_index(creds, i);
        char *a = esc(c->origin), *b = esc(c->user), *d = esc(c->pass);
        g_string_append_printf(pt, "%s\t%s\t%s\n", a, b, d);
        g_free(a); g_free(b); g_free(d);
    }
    unsigned char iv[12], tag[16];
    gcry_randomize(iv, sizeof iv, GCRY_STRONG_RANDOM);
    unsigned char *ct = g_malloc(pt->len + 1);
    gcry_cipher_hd_t h; int rc = -1;
    if (!gcry_cipher_open(&h, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, 0)) {
        if (!gcry_cipher_setkey(h, vkey, 32) && !gcry_cipher_setiv(h, iv, sizeof iv) &&
            !gcry_cipher_encrypt(h, ct, pt->len, pt->str, pt->len) && !gcry_cipher_gettag(h, tag, sizeof tag)) rc = 0;
        gcry_cipher_close(h);
    }
    if (rc == 0) {
        GByteArray *out = g_byte_array_new();
        g_byte_array_append(out, (const guint8 *)"CPW1", 4);
        g_byte_array_append(out, vsalt, 16); g_byte_array_append(out, iv, 12); g_byte_array_append(out, tag, 16);
        g_byte_array_append(out, ct, pt->len);
        char *p = data_path("passwords.vault"), *tmp = g_strconcat(p, ".tmp", NULL);
        if (g_file_set_contents(tmp, (const char *)out->data, out->len, NULL)) { g_chmod(tmp, 0600); rc = g_rename(tmp, p); } else rc = -1;
        g_free(p); g_free(tmp); g_byte_array_free(out, TRUE);
    }
    memset(pt->str, 0, pt->len); g_string_free(pt, TRUE); g_free(ct);
    return rc;
}
/* crea una bóveda vacía con esa contraseña maestra */
static int vault_create(const char *master) {
    crypto_init();
    gcry_randomize(vsalt, sizeof vsalt, GCRY_STRONG_RANDOM);
    if (derive(master, vsalt)) return -1;
    if (creds) g_ptr_array_free(creds, TRUE);
    creds = g_ptr_array_new_with_free_func(cred_free);
    if (vault_save()) return -1;
    vunlocked = 1;
    return 0;
}
/* 0 = abierta; -1 = contraseña incorrecta o archivo dañado */
static int vault_unlock(const char *master) {
    crypto_init();
    char *p = data_path("passwords.vault"); gchar *raw = NULL; gsize len = 0;
    int ok = g_file_get_contents(p, &raw, &len, NULL); g_free(p);
    if (!ok || len < 48 || memcmp(raw, "CPW1", 4)) { g_free(raw); return -1; }
    memcpy(vsalt, raw + 4, 16);
    unsigned char iv[12], tag[16]; memcpy(iv, raw + 20, 12); memcpy(tag, raw + 32, 16);
    gsize cl = len - 48;
    if (derive(master, vsalt)) { g_free(raw); return -1; }
    char *pt = g_malloc(cl + 1); int rc = -1; gcry_cipher_hd_t h;
    if (!gcry_cipher_open(&h, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, 0)) {
        if (!gcry_cipher_setkey(h, vkey, 32) && !gcry_cipher_setiv(h, iv, 12) &&
            !gcry_cipher_decrypt(h, pt, cl, raw + 48, cl) && !gcry_cipher_checktag(h, tag, 16)) rc = 0;
        gcry_cipher_close(h);
    }
    g_free(raw);
    if (rc) { g_free(pt); memset(vkey, 0, sizeof vkey); return -1; }
    pt[cl] = 0;
    if (creds) g_ptr_array_free(creds, TRUE);
    creds = g_ptr_array_new_with_free_func(cred_free);
    char **ls = g_strsplit(pt, "\n", -1);
    for (int i = 0; ls[i]; i++) {
        char **f = g_strsplit(ls[i], "\t", 3);
        if (f[0] && f[1] && f[2]) {
            Cred *c = g_new0(Cred, 1);
            c->origin = g_uri_unescape_string(f[0], NULL); c->user = g_uri_unescape_string(f[1], NULL); c->pass = g_uri_unescape_string(f[2], NULL);
            g_ptr_array_add(creds, c);
        }
        g_strfreev(f);
    }
    g_strfreev(ls); memset(pt, 0, cl); g_free(pt);
    vunlocked = 1;
    return 0;
}
static void vault_lock(void) {
    if (creds) { g_ptr_array_free(creds, TRUE); creds = NULL; }
    memset(vkey, 0, sizeof vkey); vunlocked = 0;
}
static Cred *cred_find(const char *origin, const char *user) {
    if (!creds) return NULL;
    for (guint i = 0; i < creds->len; i++) {
        Cred *c = g_ptr_array_index(creds, i);
        if (!strcmp(c->origin, origin) && (!user || !strcmp(c->user, user))) return c;
    }
    return NULL;
}
/* guarda o actualiza; devuelve 1 si cambió algo */
static int cred_put(const char *origin, const char *user, const char *pass) {
    Cred *c = cred_find(origin, user);
    if (c) {
        if (!strcmp(c->pass, pass)) return 0;
        memset(c->pass, 0, strlen(c->pass)); g_free(c->pass); c->pass = g_strdup(pass);
    } else {
        c = g_new0(Cred, 1); c->origin = g_strdup(origin); c->user = g_strdup(user); c->pass = g_strdup(pass);
        g_ptr_array_add(creds, c);
    }
    vault_save();
    return 1;
}
static void cred_remove(guint idx) { if (creds && idx < creds->len) { g_ptr_array_remove_index(creds, idx); vault_save(); } }

#endif
