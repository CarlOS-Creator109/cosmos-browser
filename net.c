/* Cosmos Browser - red (vía curl), archivos locales, URLs */
#include "cosmos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <strings.h>

#define UA "Mozilla/5.0 (X11; Linux x86_64) CosmosBrowser/" COSMOS_VERSION " (COS-Mods)"

uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    if (len) *len = got;
    return b;
}

void resp_free(Resp *r) { free(r->data); r->data = NULL; r->len = 0; }

/* ---------- buffer de texto ---------- */
typedef struct { char *s; size_t n, cap; } SB;
static void sb_add(SB *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void sb_str(SB *b, const char *s) { sb_add(b, s, strlen(s)); }
static void sb_esc(SB *b, const char *s) {
    for (; *s; s++) {
        if (*s == '<') sb_str(b, "&lt;");
        else if (*s == '>') sb_str(b, "&gt;");
        else if (*s == '&') sb_str(b, "&amp;");
        else if (*s == '"') sb_str(b, "&quot;");
        else sb_add(b, s, 1);
    }
}

/* ---------- %XX ---------- */
static void url_decode(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            out[o++] = (char)strtol(h, NULL, 16);
            s += 2;
        } else out[o++] = *s;
    }
    out[o] = 0;
}
static void url_encode_path(const char *s, SB *b) {
    for (; *s; s++) {
        unsigned char c = *s;
        if (isalnum(c) || strchr("/-_.~", c) || c >= 0x80) sb_add(b, (char *)&c, 1);
        else { char t[4]; snprintf(t, sizeof t, "%%%02X", c); sb_str(b, t); }
    }
}
static void url_encode_query(const char *s, SB *b) {
    for (; *s; s++) {
        unsigned char c = *s;
        if (isalnum(c) || strchr("-_.~", c)) sb_add(b, (char *)&c, 1);
        else if (c == ' ') sb_str(b, "+");
        else { char t[4]; snprintf(t, sizeof t, "%%%02X", c); sb_str(b, t); }
    }
}

/* ---------- tipo MIME por extensión ---------- */
static const char *mime_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "text/plain";
    struct { const char *e, *m; } t[] = {
        {".html", "text/html"}, {".htm", "text/html"}, {".xhtml", "text/html"},
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"}, {".bmp", "image/bmp"},
        {".txt", "text/plain"}, {".md", "text/plain"}, {".c", "text/plain"}, {".h", "text/plain"},
        {".css", "text/plain"}, {".js", "text/plain"}, {".json", "text/plain"}, {".py", "text/plain"},
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!strcasecmp(dot, t[i].e)) return t[i].m;
    return "application/octet-stream";
}

/* Ejecuta un programa y captura su stdout completo. Devuelve el código de salida (127 si no existe). */
int run_capture(char *const argv[], const char *errfile, char **out, volatile int *pid) {
    int pfd[2];
    *out = NULL;
    if (pipe(pfd) != 0) return -1;
    pid_t c = fork();
    if (c < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (c == 0) {
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) dup2(dn, 0);
        dup2(pfd[1], 1);
        int ef = errfile ? open(errfile, O_WRONLY | O_CREAT | O_TRUNC, 0600) : open("/dev/null", O_WRONLY);
        if (ef >= 0) dup2(ef, 2);
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (pid) *pid = c;
    close(pfd[1]);
    size_t n = 0, cap = 4096;
    char *buf = malloc(cap);
    for (;;) {
        if (n + 1024 > cap) { cap *= 2; buf = realloc(buf, cap); }
        ssize_t k = read(pfd[0], buf + n, cap - n - 1);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        n += (size_t)k;
        if (n > (64u << 20)) break;
    }
    buf[n] = 0;
    close(pfd[0]);
    int st = 0;
    waitpid(c, &st, 0);
    *out = buf;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int fetch_ytsearch(const char *q, Resp *r) {
    char query[512];
    url_decode(q, query, sizeof query);
    for (char *c = query; *c; c++) if (*c == '+') *c = ' ';
    SB b = {0};
    sb_str(&b, "<html><head><title>YouTube</title><style>a{color:#5de0ff}</style></head><body style=\"background:#0b0d1e;color:#dfe3ff\">"
               "<h2 style=\"color:#5de0ff\">YouTube: ");
    sb_esc(&b, query);
    sb_str(&b, "</h2>");
    const char *yt = getenv("COSMOS_YTDLP") ? getenv("COSMOS_YTDLP") : "yt-dlp";
    char spec[600];
    snprintf(spec, sizeof spec, "ytsearch12:%s", query);
    char *argv[] = { (char *)yt, "-q", "--no-warnings", "--flat-playlist", "--print",
                     "%(id)s\t%(title)s\t%(duration_string)s\t%(channel)s", spec, NULL };
    char *out = NULL;
    int rc = run_capture(argv, NULL, &out, NULL);
    if (rc == 127) sb_str(&b, "<p>No encontr\xc3\xa9 <b>yt-dlp</b>. Inst\xc3\xa1lalo con: <code>sudo pacman -S yt-dlp ffmpeg</code></p>");
    else if (rc != 0 || !out || !*out) sb_str(&b, "<p>La b\xc3\xbasqueda fall\xc3\xb3. Revisa tu conexi\xc3\xb3n o actualiza yt-dlp (<code>yt-dlp -U</code>).</p>");
    else {
        char *line = out;
        while (line && *line) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
            char *f[4] = { line, "", "", "" };
            int nf = 1;
            for (char *c = line; *c && nf < 4; c++) if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
            if (nf >= 2 && f[0][0]) {
                sb_str(&b, "<p><a href=\"https://www.youtube.com/watch?v=");
                sb_esc(&b, f[0]);
                sb_str(&b, "\"><img src=\"https://i.ytimg.com/vi/");
                sb_esc(&b, f[0]);
                sb_str(&b, "/mqdefault.jpg\" width=\"160\"> ");
                sb_esc(&b, f[1]);
                sb_str(&b, "</a><br>");
                sb_esc(&b, f[3]);
                if (f[2][0] && strcmp(f[2], "NA")) { sb_str(&b, " &middot; "); sb_esc(&b, f[2]); }
                sb_str(&b, "</p>");
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    free(out);
    sb_str(&b, "<p><a href=\"cosmos:home\">Inicio</a></p></body></html>");
    r->data = (uint8_t *)b.s; r->len = b.n;
    strcpy(r->ctype, "text/html");
    r->status = 200;
    return 0;
}

char cosmos_dir[1024] = ".";
static int fetch_builtin(const char *url, Resp *r) {
    const char *name = url + 7; /* "cosmos:" */
    if (!strcmp(name, "logo.png")) {
        char path[1100];
        snprintf(path, sizeof path, "%s/logo.png", cosmos_dir);
        r->data = read_file(path, &r->len);
        if (!r->data) { snprintf(r->err, sizeof r->err, "Falta logo.png junto al ejecutable"); return -1; }
        strcpy(r->ctype, "image/png");
        strncpy(r->final_url, url, sizeof r->final_url - 1);
        r->status = 200;
        return 0;
    }
    if (!strncmp(name, "yt?q=", 5)) {
        int rr = fetch_ytsearch(name + 5, r);
        strncpy(r->final_url, url, sizeof r->final_url - 1);
        return rr;
    }
    const char *p = builtin_page(name);
    if (!p) { snprintf(r->err, sizeof r->err, "No existe la página interna \"%s\"", name); return -1; }
    r->len = strlen(p);
    r->data = malloc(r->len + 1);
    memcpy(r->data, p, r->len + 1);
    strcpy(r->ctype, "text/html");
    strncpy(r->final_url, url, sizeof r->final_url - 1);
    r->status = 200;
    return 0;
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int fetch_file(const char *url, Resp *r) {
    char path[1536];
    url_decode(url + 7, path, sizeof path);       /* file:///ruta -> /ruta */
    char *q = strchr(path, '#'); if (q) *q = 0;
    struct stat st;
    if (stat(path, &st) != 0) { snprintf(r->err, sizeof r->err, "No se encontró el archivo: %s", strerror(errno)); return -1; }
    strncpy(r->final_url, url, sizeof r->final_url - 1);
    r->status = 200;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) { snprintf(r->err, sizeof r->err, "No se pudo abrir la carpeta: %s", strerror(errno)); return -1; }
        char **names = NULL; int nn = 0, cap = 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".")) continue;
            if (nn == cap) { cap = cap ? cap * 2 : 64; names = realloc(names, cap * sizeof *names); }
            names[nn++] = strdup(e->d_name);
        }
        closedir(d);
        qsort(names, nn, sizeof *names, cmp_names);
        SB b = {0};
        sb_str(&b, "<html><body style=\"background:#0b0d1e;color:#dfe3ff\"><h2 style=\"color:#5de0ff\">Carpeta: ");
        sb_esc(&b, path);
        sb_str(&b, "</h2><ul style=\"list-style:none\">");
        for (int i = 0; i < nn; i++) {
            char full[2048];
            snprintf(full, sizeof full, "%s%s%s", path, (path[strlen(path) - 1] == '/') ? "" : "/", names[i]);
            struct stat s2;
            int isdir = (stat(full, &s2) == 0 && S_ISDIR(s2.st_mode));
            SB enc = {0};
            url_encode_path(full, &enc);
            sb_str(&b, "<li><a style=\"color:#9bd0ff\" href=\"file://");
            sb_str(&b, enc.s);
            sb_str(&b, "\">");
            sb_str(&b, isdir ? "\xe2\x96\xb8 " : "\xe2\x80\xa2 ");
            sb_esc(&b, names[i]);
            if (isdir) sb_str(&b, "/");
            sb_str(&b, "</a></li>");
            free(enc.s);
            free(names[i]);
        }
        free(names);
        sb_str(&b, "</ul></body></html>");
        r->data = (uint8_t *)b.s; r->len = b.n;
        strcpy(r->ctype, "text/html");
        return 0;
    }
    r->data = read_file(path, &r->len);
    if (!r->data) { snprintf(r->err, sizeof r->err, "No se pudo leer el archivo: %s", strerror(errno)); return -1; }
    strncpy(r->ctype, mime_ext(path), sizeof r->ctype - 1);
    return 0;
}

static int fetch_http(const char *url, Resp *r, size_t maxlen, int timeout_s) {
    char tmpl[] = "/tmp/cosmos-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { snprintf(r->err, sizeof r->err, "No se pudo crear archivo temporal"); return -1; }
    close(fd);
    int pfd[2];
    if (pipe(pfd) != 0) { unlink(tmpl); snprintf(r->err, sizeof r->err, "pipe falló"); return -1; }
    char tbuf[16], mbuf[32];
    snprintf(tbuf, sizeof tbuf, "%d", timeout_s);
    snprintf(mbuf, sizeof mbuf, "%zu", maxlen);
    pid_t pid = fork();
    if (pid < 0) { unlink(tmpl); snprintf(r->err, sizeof r->err, "fork falló"); return -1; }
    if (pid == 0) {
        dup2(pfd[1], 1);
        close(pfd[0]); close(pfd[1]);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 2); close(dn); }
        execlp("curl", "curl", "-sL", "--compressed", "--max-time", tbuf, "--max-filesize", mbuf,
               "--max-redirs", "10", "-A", UA, "-o", tmpl,
               "-w", "%{url_effective}\t%{content_type}\t%{http_code}", "--", url, (char *)NULL);
        _exit(127);
    }
    close(pfd[1]);
    char out[4096]; size_t on = 0;
    ssize_t k;
    while ((k = read(pfd[0], out + on, sizeof out - 1 - on)) > 0) { on += k; if (on >= sizeof out - 1) break; }
    close(pfd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    out[on] = 0;
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code != 0) {
        unlink(tmpl);
        switch (code) {
        case 6: snprintf(r->err, sizeof r->err, "No se pudo encontrar el servidor (¿sin internet o DNS?)"); break;
        case 7: snprintf(r->err, sizeof r->err, "El servidor rechazó la conexión"); break;
        case 28: snprintf(r->err, sizeof r->err, "Se agotó el tiempo de espera"); break;
        case 35: case 51: case 58: case 59: case 60: case 77: case 83:
            snprintf(r->err, sizeof r->err, "Error de certificado / TLS (curl %d)", code); break;
        case 63: snprintf(r->err, sizeof r->err, "El archivo es demasiado grande"); break;
        case 127: snprintf(r->err, sizeof r->err, "No se encontró curl; instálalo (pacman -S curl)"); break;
        default: snprintf(r->err, sizeof r->err, "Error de red (curl %d)", code);
        }
        return -1;
    }
    char *t1 = strchr(out, '\t');
    char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    if (t1 && t2) {
        *t1 = 0; *t2 = 0;
        strncpy(r->final_url, out, sizeof r->final_url - 1);
        strncpy(r->ctype, t1 + 1, sizeof r->ctype - 1);
        r->status = atoi(t2 + 1);
    } else strncpy(r->final_url, url, sizeof r->final_url - 1);
    for (char *c = r->ctype; *c; c++) *c = tolower((unsigned char)*c);
    r->data = read_file(tmpl, &r->len);
    unlink(tmpl);
    if (!r->data) { snprintf(r->err, sizeof r->err, "No se pudo leer la respuesta"); return -1; }
    return 0;
}

int fetch_url(const char *url, Resp *r, size_t maxlen, int timeout_s) {
    memset(r, 0, sizeof *r);
    if (!strncmp(url, "cosmos:", 7)) return fetch_builtin(url, r);
    if (!strncmp(url, "file://", 7)) return fetch_file(url, r);
    if (!strncmp(url, "http://", 7) || !strncmp(url, "https://", 8)) return fetch_http(url, r, maxlen, timeout_s);
    snprintf(r->err, sizeof r->err, "Protocolo no soportado: %.40s", url);
    return -1;
}

/* ---------- resolver URLs ---------- */
static void remove_dots(char *path) {
    /* path empieza con '/' ; quita ./ y ../ */
    char *segs[256]; int n = 0;
    char *copy = strdup(path);
    char *save = NULL;
    int trailing = strlen(path) > 0 && path[strlen(path) - 1] == '/';
    for (char *s = strtok_r(copy, "/", &save); s; s = strtok_r(NULL, "/", &save)) {
        if (!strcmp(s, ".")) continue;
        if (!strcmp(s, "..")) { if (n > 0) n--; continue; }
        if (n < 256) segs[n++] = s;
    }
    char *o = path;
    for (int i = 0; i < n; i++) { *o++ = '/'; size_t l = strlen(segs[i]); memmove(o, segs[i], l); o += l; }
    if (n == 0 || trailing) *o++ = '/';
    *o = 0;
    free(copy);
}

static int has_scheme(const char *s) {
    if (!isalpha((unsigned char)*s)) return 0;
    const char *p = s + 1;
    while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.') p++;
    return *p == ':';
}

void url_resolve(const char *base, const char *ref, char *out, size_t cap) {
    char r[2048];
    while (isspace((unsigned char)*ref)) ref++;
    snprintf(r, sizeof r, "%s", ref);
    size_t rl = strlen(r);
    while (rl && isspace((unsigned char)r[rl - 1])) r[--rl] = 0;
    if (has_scheme(r)) { snprintf(out, cap, "%s", r); return; }
    if (!base || !*base) { snprintf(out, cap, "%s", r); return; }
    const char *sep = strstr(base, "://");
    if (!sep) {                                  /* base tipo cosmos:home */
        snprintf(out, cap, "%s", r);
        return;
    }
    char scheme[16]; size_t sl = sep - base;
    if (sl >= sizeof scheme) sl = sizeof scheme - 1;
    memcpy(scheme, base, sl); scheme[sl] = 0;
    if (r[0] == '/' && r[1] == '/') { snprintf(out, cap, "%s:%s", scheme, r); return; }
    const char *auth = sep + 3;
    const char *pe = auth + strcspn(auth, "/?#");           /* fin de autoridad */
    char origin[1024];
    size_t ol = pe - base; if (ol >= sizeof origin) ol = sizeof origin - 1;
    memcpy(origin, base, ol); origin[ol] = 0;
    char bpath[1536] = "/";
    const char *pq = pe + strcspn(pe, "?#");
    if (pq > pe) { size_t pl = pq - pe; if (pl >= sizeof bpath) pl = sizeof bpath - 1; memcpy(bpath, pe, pl); bpath[pl] = 0; }
    char bnofrag[2048];
    snprintf(bnofrag, sizeof bnofrag, "%s", base);
    char *h = strchr(bnofrag, '#'); if (h) *h = 0;
    if (r[0] == 0) { snprintf(out, cap, "%s", bnofrag); return; }
    if (r[0] == '#') { snprintf(out, cap, "%s%s", bnofrag, r); return; }
    if (r[0] == '?') {
        char b2[2048]; snprintf(b2, sizeof b2, "%s", bnofrag);
        char *qm = strchr(b2, '?'); if (qm) *qm = 0;
        snprintf(out, cap, "%s%s", b2, r);
        return;
    }
    char np[3072];
    if (r[0] == '/') snprintf(np, sizeof np, "%s", r);
    else {
        char dir[1536]; snprintf(dir, sizeof dir, "%s", bpath);
        char *ls = strrchr(dir, '/'); if (ls) ls[1] = 0;
        snprintf(np, sizeof np, "%s%s", dir, r);
    }
    /* separar ruta de ?query#frag */
    char tail[1024] = "";
    size_t cut = strcspn(np, "?#");
    snprintf(tail, sizeof tail, "%s", np + cut);
    np[cut] = 0;
    remove_dots(np);
    snprintf(out, cap, "%s%s%s", origin, np, tail);
}

void omnibox_to_url(const char *in, char *out, size_t cap) {
    char s[2048];
    while (isspace((unsigned char)*in)) in++;
    snprintf(s, sizeof s, "%s", in);
    size_t l = strlen(s);
    while (l && isspace((unsigned char)s[l - 1])) s[--l] = 0;
    if (!l) { snprintf(out, cap, "cosmos:home"); return; }
    if (!strncasecmp(s, "yt ", 3) || !strncasecmp(s, "youtube ", 8)) {
        const char *qq = s + (s[1] == 't' || s[1] == 'T' ? 3 : 8);
        while (*qq == ' ') qq++;
        SB b = {0};
        sb_str(&b, "cosmos:yt?q=");
        url_encode_query(qq, &b);
        snprintf(out, cap, "%s", b.s);
        free(b.s);
        return;
    }
    if (!strcmp(s, "about:") || !strcmp(s, "about:cosmos")) { snprintf(out, cap, "cosmos:about"); return; }
    if (strstr(s, "://") || !strncmp(s, "cosmos:", 7) || !strncmp(s, "data:", 5)) { snprintf(out, cap, "%s", s); return; }
    int is_path = (s[0] == '/' || !strncmp(s, "~/", 2) || !strncmp(s, "./", 2) || !strncmp(s, "../", 3));
    if (!is_path && access(s, F_OK) == 0 && !strchr(s, ' ')) is_path = 1;
    if (is_path) {
        char abs[2048];
        if (s[0] == '/') snprintf(abs, sizeof abs, "%s", s);
        else if (s[0] == '~') snprintf(abs, sizeof abs, "%s%s", getenv("HOME") ? getenv("HOME") : "", s + 1);
        else {
            char cwd[1024];
            if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "/");
            snprintf(abs, sizeof abs, "%s/%s", cwd, s);
        }
        SB b = {0};
        sb_str(&b, "file://");
        url_encode_path(abs, &b);
        snprintf(out, cap, "%s", b.s);
        free(b.s);
        return;
    }
    /* ¿parece dominio? */
    int has_space = strchr(s, ' ') != NULL;
    char host[256];
    size_t hl = strcspn(s, "/?#");
    snprintf(host, sizeof host, "%.*s", (int)(hl < 255 ? hl : 255), s);
    char *colon = strchr(host, ':'); if (colon) *colon = 0;
    int dom = 0, ip = 0;
    if (!has_space && host[0]) {
        if (!strcmp(host, "localhost")) { dom = 1; ip = 1; }
        else {
            const char *dot = strrchr(host, '.');
            int alldig = 1;
            for (const char *c = host; *c; c++) if (!isdigit((unsigned char)*c) && *c != '.') alldig = 0;
            if (alldig && dot) { dom = 1; ip = 1; }
            else if (dot && dot != host && strlen(dot + 1) >= 2) {
                int alpha = 1;
                for (const char *c = dot + 1; *c; c++) if (!isalpha((unsigned char)*c)) alpha = 0;
                dom = alpha;
            }
        }
    }
    if (dom) { snprintf(out, cap, "%s://%s", ip ? "http" : "https", s); return; }
    SB b = {0};
    sb_str(&b, "https://html.duckduckgo.com/html/?q=");
    url_encode_query(s, &b);
    snprintf(out, cap, "%s", b.s);
    free(b.s);
}
