/* Cosmos Browser - gráficos por software: framebuffer, fuentes TTF (stb), imágenes */
#include "cosmos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb_image.h"

/* ---------- arena ---------- */
struct ABlk { ABlk *next; size_t used, cap; char data[]; };

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > 65536 ? n : 65536;
        ABlk *b = malloc(sizeof(ABlk) + cap);
        if (!b) { fprintf(stderr, "sin memoria\n"); exit(1); }
        b->next = a->head; b->used = 0; b->cap = cap; a->head = b;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}
char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *p = arena_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}
void arena_free(Arena *a) {
    ABlk *b = a->head;
    while (b) { ABlk *nx = b->next; free(b); b = nx; }
    a->head = NULL;
}

/* ---------- utf8 ---------- */
int utf8_next(const char **pp, const char *end) {
    const unsigned char *p = (const unsigned char *)*pp;
    int c = *p, n = 0;
    if (c < 0x80) { *pp += 1; return c; }
    if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { *pp += 1; return 0xFFFD; }
    if ((const char *)(p + n) >= end) { *pp = end; return 0xFFFD; }
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *pp += i; return 0xFFFD; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *pp += n + 1;
    return c;
}
int utf8_put(char *o, int cp) {
    if (cp < 0x80) { o[0] = cp; return 1; }
    if (cp < 0x800) { o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 63); return 2; }
    if (cp < 0x10000) { o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 63); o[2] = 0x80 | (cp & 63); return 3; }
    o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 63); o[2] = 0x80 | ((cp >> 6) & 63); o[3] = 0x80 | (cp & 63); return 4;
}

/* ---------- fuentes ---------- */
static stbtt_fontinfo fi[NFONTS];
static uint8_t *fdata[NFONTS];
static int fok[NFONTS];
static const char *fnames[NFONTS] = {
    "DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSans-Oblique.ttf",
    "DejaVuSansMono.ttf", "DejaVuSansMono-Bold.ttf"
};

int gfx_init(const char *extra) {
    const char *dirs[8]; int nd = 0;
    if (extra) dirs[nd++] = extra;
    dirs[nd++] = "./fonts";
    dirs[nd++] = "/usr/share/cosmos-browser/fonts";
    dirs[nd++] = "/usr/share/fonts/TTF";
    dirs[nd++] = "/usr/share/fonts/truetype/dejavu";
    dirs[nd++] = "/usr/share/fonts/dejavu";
    for (int f = 0; f < NFONTS; f++) {
        for (int d = 0; d < nd && !fok[f]; d++) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dirs[d], fnames[f]);
            size_t len;
            uint8_t *buf = read_file(path, &len);
            if (!buf) continue;
            if (stbtt_InitFont(&fi[f], buf, stbtt_GetFontOffsetForIndex(buf, 0))) { fdata[f] = buf; fok[f] = 1; }
            else free(buf);
        }
    }
    return fok[0] ? 0 : -1;
}

#define GCN 8192
typedef struct { int font, px, cp; unsigned char *bmp; int w, h, xo, yo, adv; char used; } Glyph;
static Glyph gc[GCN];
static int gcount;

static Glyph *glyph(int font, int px, int cp) {
    if (!fok[font]) font = 0;
    if (cp == 0xA0) cp = ' ';
    unsigned h = ((unsigned)cp * 2654435761u) ^ ((unsigned)font * 40503u) ^ ((unsigned)px * 9176u);
    h %= GCN;
    for (;;) {
        Glyph *g = &gc[h];
        if (!g->used) break;
        if (g->cp == cp && g->font == font && g->px == px) return g;
        h = (h + 1) % GCN;
    }
    if (gcount > GCN * 3 / 4) {
        for (int i = 0; i < GCN; i++) free(gc[i].bmp);
        memset(gc, 0, sizeof gc);
        gcount = 0;
        return glyph(font, px, cp);
    }
    Glyph *g = &gc[h];
    stbtt_fontinfo *f = &fi[font];
    float sc = stbtt_ScaleForMappingEmToPixels(f, (float)px);
    int gi = stbtt_FindGlyphIndex(f, cp);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(f, gi, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(f, gi, sc, sc, &x0, &y0, &x1, &y1);
    g->used = 1; g->font = font; g->px = px; g->cp = cp;
    g->w = x1 - x0; g->h = y1 - y0; g->xo = x0; g->yo = y0;
    g->adv = (int)(adv * sc + 0.5f);
    if (g->w > 0 && g->h > 0) {
        g->bmp = malloc((size_t)g->w * g->h);
        stbtt_MakeGlyphBitmap(f, g->bmp, g->w, g->h, g->w, sc, sc, gi);
    }
    gcount++;
    return g;
}

int font_asc(int font, int px) {
    if (!fok[font]) font = 0;
    int a, d, l;
    stbtt_GetFontVMetrics(&fi[font], &a, &d, &l);
    return (int)(a * stbtt_ScaleForMappingEmToPixels(&fi[font], (float)px) + 0.5f);
}
int font_desc(int font, int px) {
    if (!fok[font]) font = 0;
    int a, d, l;
    stbtt_GetFontVMetrics(&fi[font], &a, &d, &l);
    return (int)(-d * stbtt_ScaleForMappingEmToPixels(&fi[font], (float)px) + 0.5f);
}
int text_w(int font, int px, const char *s, int len) {
    const char *p = s, *e = s + len;
    int w = 0;
    while (p < e) { int cp = utf8_next(&p, e); w += glyph(font, px, cp)->adv; }
    return w;
}

/* ---------- framebuffer ---------- */
void fb_init(Fb *f, int w, int h) {
    f->w = w; f->h = h;
    f->px = calloc((size_t)w * h, 4);
    fb_noclip(f);
}
void fb_resize(Fb *f, int w, int h) {
    free(f->px);
    fb_init(f, w, h);
}
void fb_noclip(Fb *f) { f->cx0 = 0; f->cy0 = 0; f->cx1 = f->w; f->cy1 = f->h; }
void fb_clip(Fb *f, int x, int y, int w, int h) {
    f->cx0 = x < 0 ? 0 : x; f->cy0 = y < 0 ? 0 : y;
    f->cx1 = x + w > f->w ? f->w : x + w; f->cy1 = y + h > f->h ? f->h : y + h;
}
static inline uint32_t blend(uint32_t d, uint32_t s, int a) {
    if (a >= 255) return s;
    if (a <= 0) return d;
    int ia = 255 - a;
    int r = (((d >> 16) & 255) * ia + ((s >> 16) & 255) * a + 127) / 255;
    int g = (((d >> 8) & 255) * ia + ((s >> 8) & 255) * a + 127) / 255;
    int b = ((d & 255) * ia + (s & 255) * a + 127) / 255;
    return (uint32_t)(r << 16 | g << 8 | b);
}
void fb_rect(Fb *f, int x, int y, int w, int h, uint32_t c) {
    int x0 = x < f->cx0 ? f->cx0 : x, y0 = y < f->cy0 ? f->cy0 : y;
    int x1 = x + w > f->cx1 ? f->cx1 : x + w, y1 = y + h > f->cy1 ? f->cy1 : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = f->px + (size_t)j * f->w;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}
void fb_frame(Fb *f, int x, int y, int w, int h, uint32_t c) {
    fb_rect(f, x, y, w, 1, c); fb_rect(f, x, y + h - 1, w, 1, c);
    fb_rect(f, x, y, 1, h, c); fb_rect(f, x + w - 1, y, 1, h, c);
}
void fb_rrect(Fb *f, int x, int y, int w, int h, int r, uint32_t c) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        int dy = -1;
        if (j < r) dy = r - j - 1; else if (j >= h - r) dy = j - (h - r);
        if (dy >= 0) {
            float fy = dy + 0.5f;
            inset = r - (int)floorf(sqrtf((float)r * r - fy * fy) + 0.5f);
        }
        fb_rect(f, x + inset, y + j, w - 2 * inset, 1, c);
    }
}
int fb_text(Fb *f, int x, int y, int font, int px, uint32_t c, const char *s, int len) {
    const char *p = s, *e = s + len;
    int base = y + font_asc(font, px), x0 = x;
    while (p < e) {
        int cp = utf8_next(&p, e);
        Glyph *g = glyph(font, px, cp);
        if (g->bmp) {
            int gx = x + g->xo, gy = base + g->yo;
            int ys = gy < f->cy0 ? f->cy0 - gy : 0, ye = gy + g->h > f->cy1 ? f->cy1 - gy : g->h;
            int xs = gx < f->cx0 ? f->cx0 - gx : 0, xe = gx + g->w > f->cx1 ? f->cx1 - gx : g->w;
            for (int j = ys; j < ye; j++) {
                uint32_t *row = f->px + (size_t)(gy + j) * f->w + gx;
                const unsigned char *src = g->bmp + (size_t)j * g->w;
                for (int i = xs; i < xe; i++) if (src[i]) row[i] = blend(row[i], c, src[i]);
            }
        }
        x += g->adv;
    }
    return x - x0;
}

/* ---------- imágenes ---------- */
void image_free(Image *im) { if (im) { free(im->px); free(im); } }

static Image *downscale(Image *im, int k) {
    int nw = im->w / k, nh = im->h / k;
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    Image *o = malloc(sizeof *o);
    o->w = nw; o->h = nh; o->px = malloc((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            int a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int j = 0; j < k; j++)
                for (int i = 0; i < k; i++) {
                    int sx = x * k + i, sy = y * k + j;
                    if (sx >= im->w || sy >= im->h) continue;
                    uint32_t p = im->px[(size_t)sy * im->w + sx];
                    a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
                }
            if (!n) n = 1;
            o->px[(size_t)y * nw + x] = ((uint32_t)(a / n) << 24) | ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
        }
    image_free(im);
    return o;
}

Image *image_decode(const uint8_t *data, size_t len) {
    int w, h, n;
    if (len > 0x7fffffff) return NULL;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &n)) return NULL;
    if ((long)w * h > 40000000L) return NULL;
    unsigned char *rgba = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!rgba) return NULL;
    Image *im = malloc(sizeof *im);
    im->w = w; im->h = h; im->px = malloc((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned char *q = rgba + i * 4;
        im->px[i] = ((uint32_t)q[3] << 24) | ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | (uint32_t)q[2];
    }
    stbi_image_free(rgba);
    int m = w > h ? w : h, k = 1;
    while (m / k > 1600) k++;
    if (k > 1) im = downscale(im, k);
    return im;
}

void fb_image(Fb *f, Image *im, int dx, int dy, int dw, int dh) {
    if (!im || dw <= 0 || dh <= 0) return;
    int x0 = dx < f->cx0 ? f->cx0 : dx, y0 = dy < f->cy0 ? f->cy0 : dy;
    int x1 = dx + dw > f->cx1 ? f->cx1 : dx + dw, y1 = dy + dh > f->cy1 ? f->cy1 : dy + dh;
    if (x0 >= x1 || y0 >= y1) return;
    int exact = (dw == im->w && dh == im->h);
    float sx = (float)im->w / dw, sy = (float)im->h / dh;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = f->px + (size_t)y * f->w;
        float fy = (y - dy + 0.5f) * sy - 0.5f;
        int iy = (int)floorf(fy); float ty = fy - iy;
        int iy0 = iy < 0 ? 0 : (iy >= im->h ? im->h - 1 : iy);
        int iy1 = iy + 1 < 0 ? 0 : (iy + 1 >= im->h ? im->h - 1 : iy + 1);
        for (int x = x0; x < x1; x++) {
            uint32_t p;
            if (exact) p = im->px[(size_t)(y - dy) * im->w + (x - dx)];
            else {
                float fx = (x - dx + 0.5f) * sx - 0.5f;
                int ix = (int)floorf(fx); float tx = fx - ix;
                int ix0 = ix < 0 ? 0 : (ix >= im->w ? im->w - 1 : ix);
                int ix1 = ix + 1 < 0 ? 0 : (ix + 1 >= im->w ? im->w - 1 : ix + 1);
                uint32_t p00 = im->px[(size_t)iy0 * im->w + ix0], p10 = im->px[(size_t)iy0 * im->w + ix1];
                uint32_t p01 = im->px[(size_t)iy1 * im->w + ix0], p11 = im->px[(size_t)iy1 * im->w + ix1];
                uint32_t out = 0;
                for (int sh = 0; sh <= 24; sh += 8) {
                    float a = ((p00 >> sh) & 255) * (1 - tx) + ((p10 >> sh) & 255) * tx;
                    float b = ((p01 >> sh) & 255) * (1 - tx) + ((p11 >> sh) & 255) * tx;
                    out |= (uint32_t)(a * (1 - ty) + b * ty + 0.5f) << sh;
                }
                p = out;
            }
            row[x] = blend(row[x], p & 0xFFFFFF, p >> 24);
        }
    }
}
