/* Cosmos Browser - reproductor de video: yt-dlp resuelve la URL, ffmpeg decodifica a
 * cuadros BGRA (tubería 1) y audio PCM s16 (tubería 3); aquí solo se lee y se muestra. */
#include "cosmos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>

void (*player_audio_sink)(const void *, int) = NULL;
void (*player_audio_clear)(void) = NULL;

#define UA "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
static const int QUALS[] = { 144, 240, 360, 480, 720, 1080 };
#define NQUALS 6

struct Player {
    char page_url[2048], title[256], err[512], urls[2][2048], errfile[64];
    int nurls, direct, w, h, fps, quality;
    volatile int hw, retrying;          /* 0 software, 1 VA-API (copia a RAM), 2 VA-API (escala en GPU) */
    double resume;
    double dur, base;
    long nframes;
    volatile int state, gen, vol, muted, closing;
    volatile int ytpid;
    pid_t ff;
    int vfd, afd, running;
    pthread_t tv, ta, tres;
    uint8_t *front, *back;
    Image img;
    int have_frame, new_frame;
    pthread_mutex_t mu, ctl;
};

typedef struct { Player *p; int gen, fd; } RArg;

/* ---------- utilidades ---------- */
int is_video_url(const char *u, int *direct) {
    if (direct) *direct = 0;
    const char *s = strstr(u, "://");
    if (!s) return 0;
    s += 3;
    char host[128];
    size_t hl = strcspn(s, "/?#:");
    if (hl >= sizeof host) return 0;
    memcpy(host, s, hl); host[hl] = 0;
    const char *path = s + hl;
    if (*path == ':') path += strcspn(path, "/?#");
    if (!strncmp(host, "www.", 4)) memmove(host, host + 4, strlen(host + 4) + 1);
    if (!strncmp(host, "m.", 2)) memmove(host, host + 2, strlen(host + 2) + 1);
    if (!strcmp(host, "youtu.be")) return path[0] == '/' && path[1] && path[1] != '?';
    if (!strcmp(host, "youtube.com") || !strcmp(host, "music.youtube.com")) {
        if (!strncmp(path, "/watch", 6)) return strstr(path, "v=") != NULL;
        if (!strncmp(path, "/shorts/", 8) || !strncmp(path, "/live/", 6) || !strncmp(path, "/embed/", 7)) return 1;
        return 0;
    }
    size_t pl = strcspn(path, "?#");
    static const char *exts[] = { ".mp4", ".webm", ".mkv", ".m3u8", ".mov", ".avi", ".mp3", ".ogg", ".flac", ".m4a", NULL };
    for (int i = 0; exts[i]; i++) {
        size_t el = strlen(exts[i]);
        if (pl >= el && !strncasecmp(path + pl - el, exts[i], el)) { if (direct) *direct = 1; return 1; }
    }
    return 0;
}

/* 1 = leído completo, 0 = EOF limpio, -2 = EOF a medias, -1 = error */
static int readn(int fd, uint8_t *b, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, b + got, n - got);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (k == 0) return got ? -2 : 0;
        got += (size_t)k;
    }
    return 1;
}

static void read_tail(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    long from = n > (long)cap - 1 ? n - (long)cap + 1 : 0;
    fseek(f, from, SEEK_SET);
    size_t got = fread(out, 1, cap - 1, f);
    out[got] = 0;
    fclose(f);
    for (char *c = out; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
}

/* ---------- hilos lectores ---------- */
static void stop_av(Player *p);
static int start_av(Player *p, double at);
typedef struct { Player *p; int gen; } RetryArg;
static void *retry_thread(void *arg) {
    RetryArg *r = arg;
    Player *p = r->p;
    pthread_mutex_lock(&p->ctl);
    if (!p->closing && p->gen == r->gen) {
        double at = p->base;
        stop_av(p);
        if (start_av(p, at) != 0) { snprintf(p->err, sizeof p->err, "No pude reiniciar ffmpeg."); p->state = PL_ERROR; }
    }
    pthread_mutex_unlock(&p->ctl);
    __sync_fetch_and_sub(&p->retrying, 1);
    free(r);
    return NULL;
}

static void *video_thread(void *arg) {
    RArg *a = arg;
    Player *p = a->p;
    size_t fsz = (size_t)p->w * p->h * 4;
    for (;;) {
        int r = readn(a->fd, p->back, fsz);
        if (p->gen != a->gen || p->closing) break;
        if (r != 1) {
            if (p->nframes == 0 && p->hw > 0 && !p->closing) {    /* VA-API falló: bajar un nivel y reintentar */
                p->hw--;
                __sync_fetch_and_add(&p->retrying, 1);
                RetryArg *ra = malloc(sizeof *ra); ra->p = p; ra->gen = a->gen;
                pthread_t th;
                if (pthread_create(&th, NULL, retry_thread, ra) == 0) pthread_detach(th);
                else { __sync_fetch_and_sub(&p->retrying, 1); free(ra); p->state = PL_ERROR; snprintf(p->err, sizeof p->err, "No pude reintentar sin VA-API."); }
            } else if (p->nframes == 0) {
                char tail[400];
                read_tail(p->errfile, tail, sizeof tail);
                snprintf(p->err, sizeof p->err, "ffmpeg no entregó video. %s", tail[0] ? tail : "(sin mensaje)");
                p->state = PL_ERROR;
            } else p->state = PL_ENDED;
            break;
        }
        pthread_mutex_lock(&p->mu);
        uint8_t *t = p->front; p->front = p->back; p->back = t;
        p->have_frame = 1; p->new_frame = 1;
        p->nframes++;
        pthread_mutex_unlock(&p->mu);
    }
    free(a);
    return NULL;
}

static void *audio_thread(void *arg) {
    RArg *a = arg;
    Player *p = a->p;
    int16_t buf[4096];
    for (;;) {
        int r = readn(a->fd, (uint8_t *)buf, sizeof buf);
        if (p->gen != a->gen || p->closing || r != 1) break;
        int v = p->muted ? 0 : p->vol;
        if (v < 100) for (size_t i = 0; i < sizeof buf / 2; i++) buf[i] = (int16_t)((buf[i] * v) / 100);
        if (player_audio_sink) player_audio_sink(buf, (int)sizeof buf);
    }
    free(a);
    return NULL;
}

/* ---------- ffmpeg ---------- */
static void stop_av(Player *p) {
    p->gen++;
    if (p->ff > 0) { kill(p->ff, SIGKILL); waitpid(p->ff, NULL, 0); p->ff = 0; }
    if (player_audio_clear) player_audio_clear();
    if (p->running) {
        pthread_join(p->tv, NULL);
        pthread_join(p->ta, NULL);
        p->running = 0;
    }
    if (p->vfd >= 0) { close(p->vfd); p->vfd = -1; }
    if (p->afd >= 0) { close(p->afd); p->afd = -1; }
    if (player_audio_clear) player_audio_clear();
}

static int start_av(Player *p, double at) {
    int vp[2], ap[2];
    if (pipe(vp) != 0) return -1;
    if (pipe(ap) != 0) { close(vp[0]); close(vp[1]); return -1; }
    char sz[64], vf[256], ss[32], fps[16];
    if (p->hw >= 2)      /* decodifica y escala en la GPU; solo se descarga el cuadro ya pequeño */
        snprintf(vf, sizeof vf, "scale_vaapi=w=%d:h=%d:force_original_aspect_ratio=decrease:format=nv12,hwdownload,format=nv12,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1,fps=%d",
                 p->w, p->h, p->w, p->h, p->fps);
    else
        snprintf(vf, sizeof vf, "scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1,fps=%d",
                 p->w, p->h, p->w, p->h, p->fps);
    const char *vdev = getenv("COSMOS_VAAPI_DEVICE") ? getenv("COSMOS_VAAPI_DEVICE") : "/dev/dri/renderD128";
    snprintf(ss, sizeof ss, "%.2f", at);
    snprintf(fps, sizeof fps, "%d", p->fps);
    (void)sz;
    const char *av[64]; int n = 0;
    av[n++] = getenv("COSMOS_FFMPEG") ? getenv("COSMOS_FFMPEG") : "ffmpeg";
    av[n++] = "-hide_banner"; av[n++] = "-loglevel"; av[n++] = "error"; av[n++] = "-nostdin";
    for (int i = 0; i < p->nurls; i++) {
        if (at > 0.5) { av[n++] = "-ss"; av[n++] = ss; }
        if (i == 0 && p->hw >= 1) {
            av[n++] = "-hwaccel"; av[n++] = "vaapi"; av[n++] = "-hwaccel_device"; av[n++] = vdev;
            if (p->hw >= 2) { av[n++] = "-hwaccel_output_format"; av[n++] = "vaapi"; }
        }
        if (!strncmp(p->urls[i], "http", 4)) { av[n++] = "-user_agent"; av[n++] = UA; }
        av[n++] = "-re";
        av[n++] = "-i"; av[n++] = p->urls[i];
    }
    av[n++] = "-map"; av[n++] = "0:v:0";
    av[n++] = "-an"; av[n++] = "-vf"; av[n++] = vf;
    av[n++] = "-f"; av[n++] = "rawvideo"; av[n++] = "-pix_fmt"; av[n++] = "bgra"; av[n++] = "pipe:1";
    av[n++] = "-map"; av[n++] = p->nurls > 1 ? "1:a:0?" : "0:a:0?";
    av[n++] = "-vn"; av[n++] = "-f"; av[n++] = "s16le"; av[n++] = "-ar"; av[n++] = "44100"; av[n++] = "-ac"; av[n++] = "2"; av[n++] = "pipe:3";
    av[n] = NULL;
    pid_t pid = fork();
    if (pid < 0) { close(vp[0]); close(vp[1]); close(ap[0]); close(ap[1]); return -1; }
    if (pid == 0) {
        int vw = fcntl(vp[1], F_DUPFD, 100), aw = fcntl(ap[1], F_DUPFD, 100);
        int ef = open(p->errfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) dup2(dn, 0);
        if (ef >= 0) dup2(ef, 2);
        dup2(vw, 1);
        dup2(aw, 3);
        for (int fd = 4; fd < 1024; fd++) close(fd);
        execvp(av[0], (char *const *)av);
        _exit(127);
    }
    close(vp[1]); close(ap[1]);
    p->ff = pid; p->vfd = vp[0]; p->afd = ap[0];
    p->base = at; p->nframes = 0;
    p->gen++;
    RArg *a = malloc(sizeof *a); a->p = p; a->gen = p->gen; a->fd = p->vfd;
    RArg *b = malloc(sizeof *b); b->p = p; b->gen = p->gen; b->fd = p->afd;
    pthread_create(&p->tv, NULL, video_thread, a);
    pthread_create(&p->ta, NULL, audio_thread, b);
    p->running = 1;
    p->state = PL_PLAYING;
    return 0;
}

/* ---------- resolución (yt-dlp) ---------- */
static void *resolve_thread(void *arg) {
    Player *p = arg;
    if (p->direct) {
        snprintf(p->urls[0], sizeof p->urls[0], "%s", p->page_url);
        p->nurls = 1;
        const char *sl = strrchr(p->page_url, '/');
        snprintf(p->title, sizeof p->title, "%s", sl ? sl + 1 : p->page_url);
        char *q = strpbrk(p->title, "?#"); if (q) *q = 0;
    } else {
        const char *yt = getenv("COSMOS_YTDLP") ? getenv("COSMOS_YTDLP") : "yt-dlp";
        char fmt[400];
        int H = p->quality;
        snprintf(fmt, sizeof fmt, "bv*[height<=%d][vcodec^=avc1]+ba[acodec^=mp4a]/b[height<=%d][vcodec^=avc1]/bv*[height<=%d]+ba/b[height<=%d]/b", H, H, H, H);
        char *argv[] = { (char *)yt, "-q", "--no-warnings", "--no-playlist", "-f", fmt,
                         "--print", "%(title)s", "--print", "%(duration)s", "--print", "%(urls)s",
                         p->page_url, NULL };
        char *out = NULL;
        int rc = run_capture(argv, p->errfile, &out, &p->ytpid);
        p->ytpid = 0;
        if (p->closing) { free(out); return NULL; }
        int nl = 0;
        char *lines[8];
        if (out) {
            char *s = out;
            while (*s && nl < 8) {
                lines[nl++] = s;
                char *e = strchr(s, '\n');
                if (!e) break;
                *e = 0; s = e + 1;
            }
        }
        if (rc != 0 || nl < 3) {
            char tail[400];
            read_tail(p->errfile, tail, sizeof tail);
            if (rc == 127) snprintf(p->err, sizeof p->err, "No encontré yt-dlp. Instálalo: sudo pacman -S yt-dlp ffmpeg");
            else snprintf(p->err, sizeof p->err, "yt-dlp falló (código %d). %s", rc, tail[0] ? tail : "Actualízalo con: yt-dlp -U");
            p->state = PL_ERROR;
            free(out);
            return NULL;
        }
        snprintf(p->title, sizeof p->title, "%s", lines[0]);
        p->dur = atof(lines[1]);
        for (int i = 2; i < nl && p->nurls < 2; i++) if (lines[i][0]) snprintf(p->urls[p->nurls++], sizeof p->urls[0], "%s", lines[i]);
        free(out);
        if (p->nurls == 0) { snprintf(p->err, sizeof p->err, "yt-dlp no devolvió URLs de video."); p->state = PL_ERROR; return NULL; }
    }
    pthread_mutex_lock(&p->ctl);
    if (!p->closing) {
        if (start_av(p, p->resume) != 0) { snprintf(p->err, sizeof p->err, "No pude iniciar ffmpeg (fork/pipe)."); p->state = PL_ERROR; }
    }
    pthread_mutex_unlock(&p->ctl);
    return NULL;
}

/* ---------- API ---------- */
int player_quality(Player *p) { return p->quality; }
int player_hw(Player *p) { return p->hw; }

void player_set_quality(Player *p, int dir) {     /* dir: -1 baja, +1 sube */
    if (p->state == PL_RESOLVING || p->state == PL_ERROR) return;
    int qi = 0;
    for (int i = 0; i < NQUALS; i++) if (QUALS[i] == p->quality) qi = i;
    qi += dir == 2 ? 1 : dir;                      /* dir 2: sube y da la vuelta al llegar al máximo */
    if (dir == 2 && qi >= NQUALS) qi = 0;
    if (qi < 0 || qi >= NQUALS) return;
    pthread_mutex_lock(&p->ctl);
    double pos = player_pos(p);
    int was_paused = (p->state == PL_PAUSED);
    stop_av(p);
    pthread_join(p->tres, NULL);
    p->quality = QUALS[qi];
    int nw = (p->quality * 16 / 9) & ~1, nh = p->quality & ~1;
    pthread_mutex_lock(&p->mu);
    free(p->front); free(p->back);
    p->w = nw; p->h = nh;
    p->front = calloc(1, (size_t)nw * nh * 4); p->back = calloc(1, (size_t)nw * nh * 4);
    p->img.w = nw; p->img.h = nh; p->img.px = (uint32_t *)p->front;
    p->have_frame = 0;
    pthread_mutex_unlock(&p->mu);
    p->base = pos; p->nframes = 0; p->resume = pos;
    p->nurls = 0;
    p->state = PL_RESOLVING;
    (void)was_paused;
    pthread_create(&p->tres, NULL, resolve_thread, p);
    pthread_mutex_unlock(&p->ctl);
}

Player *player_open(const char *url, int direct, int w, int h) {
    Player *p = calloc(1, sizeof *p);
    snprintf(p->page_url, sizeof p->page_url, "%s", url);
    p->direct = direct;
    p->quality = 360;
    if (getenv("COSMOS_QUALITY")) p->quality = atoi(getenv("COSMOS_QUALITY"));
    int qi = 0;
    for (int i = 0; i < NQUALS; i++) if (QUALS[i] <= p->quality) qi = i;
    p->quality = QUALS[qi];
    (void)w; (void)h;
    p->w = (p->quality * 16 / 9) & ~1; p->h = p->quality & ~1;
    p->fps = getenv("COSMOS_FPS") ? atoi(getenv("COSMOS_FPS")) : 24;
    if (p->fps < 5 || p->fps > 60) p->fps = 24;
    p->vol = 80; p->vfd = p->afd = -1;
    {   /* aceleración: COSMOS_HWACCEL = auto (defecto) | vaapi | copy | none */
        const char *hv = getenv("COSMOS_HWACCEL");
        const char *dev = getenv("COSMOS_VAAPI_DEVICE") ? getenv("COSMOS_VAAPI_DEVICE") : "/dev/dri/renderD128";
        if (hv && !strcmp(hv, "none")) p->hw = 0;
        else if (hv && !strcmp(hv, "copy")) p->hw = 1;
        else if (hv && !strcmp(hv, "vaapi")) p->hw = 2;
        else p->hw = access(dev, R_OK | W_OK) == 0 ? 2 : 0;
    }
    p->state = PL_RESOLVING;
    size_t fsz = (size_t)p->w * p->h * 4;
    p->front = calloc(1, fsz); p->back = calloc(1, fsz);
    p->img.w = p->w; p->img.h = p->h; p->img.px = (uint32_t *)p->front;
    pthread_mutex_init(&p->mu, NULL);
    pthread_mutex_init(&p->ctl, NULL);
    snprintf(p->errfile, sizeof p->errfile, "/tmp/cosmos-play-%d-%p.log", (int)getpid(), (void *)p);
    snprintf(p->title, sizeof p->title, "Cargando...");
    pthread_create(&p->tres, NULL, resolve_thread, p);
    return p;
}

void player_close(Player *p) {
    if (!p) return;
    p->closing = 1;
    while (p->retrying) usleep(1000);
    if (p->ytpid > 0) kill(p->ytpid, SIGKILL);
    pthread_join(p->tres, NULL);
    pthread_mutex_lock(&p->ctl);
    stop_av(p);
    pthread_mutex_unlock(&p->ctl);
    unlink(p->errfile);
    free(p->front); free(p->back);
    free(p);
}

double player_pos(Player *p) {
    double t = p->base + (double)p->nframes / p->fps;
    if (p->dur > 0 && t > p->dur) t = p->dur;
    return t;
}
double player_dur(Player *p) { return p->dur; }
int player_state(Player *p) { return p->state; }
int player_vol(Player *p) { return p->muted ? -1 : p->vol; }

void player_toggle_pause(Player *p) {
    pthread_mutex_lock(&p->ctl);
    if (p->state == PL_PLAYING) {
        double pos = player_pos(p);
        stop_av(p);
        p->base = pos; p->nframes = 0;
        p->state = PL_PAUSED;
    } else if (p->state == PL_PAUSED) {
        start_av(p, p->base);
    } else if (p->state == PL_ENDED && p->nurls) {
        stop_av(p);
        start_av(p, 0);
    }
    pthread_mutex_unlock(&p->ctl);
}

void player_seek_abs(Player *p, double t) {
    if (p->state == PL_RESOLVING || p->state == PL_ERROR) return;
    pthread_mutex_lock(&p->ctl);
    if (t < 0) t = 0;
    if (p->dur > 2 && t > p->dur - 1) t = p->dur - 1;
    stop_av(p);
    start_av(p, t);
    pthread_mutex_unlock(&p->ctl);
}
void player_seek(Player *p, double delta) { player_seek_abs(p, player_pos(p) + delta); }

void player_volume(Player *p, int d) {
    int v = p->vol + d;
    p->vol = v < 0 ? 0 : (v > 100 ? 100 : v);
    p->muted = 0;
}
void player_mute(Player *p) { p->muted = !p->muted; }

void player_info(Player *p, char *title, int tcap, char *err, int ecap) {
    if (title) snprintf(title, tcap, "%s", p->title);
    if (err) snprintf(err, ecap, "%s", p->err);
}

int player_new_frame(Player *p) {
    pthread_mutex_lock(&p->mu);
    int n = p->new_frame;
    p->new_frame = 0;
    pthread_mutex_unlock(&p->mu);
    return n;
}
Image *player_lock_frame(Player *p) {
    pthread_mutex_lock(&p->mu);
    if (!p->have_frame) { pthread_mutex_unlock(&p->mu); return NULL; }
    p->img.px = (uint32_t *)p->front;
    return &p->img;
}
void player_unlock_frame(Player *p) { pthread_mutex_unlock(&p->mu); }
