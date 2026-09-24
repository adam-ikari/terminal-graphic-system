/*
 * kitty_native.c — reference receiver for kitty-native graphics frames.
 * See kitty_native.h for the contract and the documented scope cuts.
 */
#include "kitty_native.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stb_image.h"

/* ── Limits (resource-size DoS bounds are policy, not accident) ─────────── */
#define KY_SLOTS        64              /* simultaneous images              */
#define KY_MAX_BINARY   (8 * 1024 * 1024)  /* decoded payload per image     */
#define KY_MAX_DIM      8192            /* per axis                         */
#define KY_MAX_PIXELS   (4 * 1024 * 1024)  /* w*h of one image               */
#define KY_AUTO_ID_BASE 0x40000000u     /* auto ids live far from app ids   */

/* ── State ──────────────────────────────────────────────────────────────── */

typedef struct {
    int      used;
    uint32_t id;
    int      w, h;
    uint32_t *px;                       /* ARGB, w*h                        */
    /* placement */
    int      placed;
    int      x0, y0;                    /* destination origin, px (cursor
                                           cell snapshot + X/Y offset)      */
    int      dw, dh;                    /* destination size, px             */
    int      sx, sy, sw, sh;            /* source crop within the image     */
    int      z;
    unsigned long seq;                  /* placement order (z tie-break)    */
} ky_slot;

static ky_slot        g_slots[KY_SLOTS];
static int            g_dirty = 1;
static unsigned long  g_seq;
static uint32_t       g_next_auto = KY_AUTO_ID_BASE;

/* Chunked base64 assembly: one transmission at a time (spec-level cut). */
static uint8_t *g_asm;
static size_t   g_asm_cap;
static size_t   g_asm_len;
static int      g_asm_on;
/* Streaming base64 carry: bits left inside an unfinished quantum. */
static unsigned g_b64_bits;
static int      g_b64_n;
/* Control keys remembered across chunks — apps repeat them; we tolerate
 * their absence on later chunks for f/w/h/i (dims and identity). */
static int      g_asm_have_f, g_asm_have_w, g_asm_have_h, g_asm_have_i;
static long     g_asm_f, g_asm_w, g_asm_h;
static long     g_asm_i;
static int      g_asm_act;              /* 't' or 'T', for an a-less end */

/* ── Parsed control data ────────────────────────────────────────────────── */

typedef struct {
    int   have_a;   int   act;          /* a=  action letter                */
    int   have_d;   int   dact;         /* d=  delete scope                 */
    int   have_f;   long  f;            /* f=  pixel format                 */
    int   have_i;   long  i;            /* i=  image id                     */
    int   have_m;   long  m;            /* m=  more chunks follow           */
    int   have_q;   long  q;            /* q=  quiet level                  */
    int   have_c;   long  c;            /* c=  display size, columns        */
    int   have_r;   long  r;            /* r=  display size, rows           */
    int   have_x;   long  x;            /* x=  source crop / cell range     */
    int   have_y;   long  y;
    int   have_w;   long  w;            /* w=  data dims / crop / cell range */
    int   have_h;   long  h;
    int   have_X;   long  X;            /* X=  in-cell pixel offset         */
    int   have_Y;   long  Y;
    int   have_z;   long  z;            /* z=  z-index                      */
    int   have_U;   long  U;            /* U=  unicode-placeholder flag     */
    int   have_p;   long  p;            /* p=  placement id                 */
    int   have_t;                       /* t=  transmit target (unsupported) */
    int   have_o;                       /* o=  shm object (unsupported)     */
    const uint8_t *data; int dlen;      /* payload after the first ';'      */
    int   parse_err;                    /* malformed value on a known key   */
} ky_keys;

/* ── Small helpers ──────────────────────────────────────────────────────── */

/* Manual digit parse — the payload is not NUL-terminated. Returns 0 ok. */
static int ky_atol(const uint8_t *s, int len, long *out)
{
    long long v = 0;
    int i = 0, neg = 0;

    if (len <= 0) return -1;
    if (s[0] == '-') { neg = 1; i = 1; }
    if (i >= len) return -1;
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
        if (v > (1LL << 40)) return -1;         /* past anything legal */
    }
    *out = neg ? -v : v;
    return 0;
}

static int b64_val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Acknowledgement: only when i= is present (the protocol's ack contract),
 * honouring q=1 (errors only) and q=2 (silent). The write is
 * poll(POLLOUT,0)+write — a program that is not reading its stdin gets a
 * dropped ack, never a blocked compositor. */
static void ky_reply(int fd, const ky_keys *k, const char *code)
{
    char buf[80];
    int n, off = 0;
    struct pollfd p;

    if (fd < 0 || !k->have_i) return;
    if (k->have_q && k->q >= 2) return;
    if (k->have_q && k->q == 1 && strcmp(code, "OK") == 0) return;

    n = snprintf(buf, sizeof buf, "\x1b_Gi=%ld,%s\x1b\\", k->i, code);
    if (n <= 0 || n >= (int)sizeof buf) return;

    p.fd = fd;
    p.events = POLLOUT;
    p.revents = 0;
    if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLOUT)) return;
    while (off < n) {
        ssize_t w = write(fd, buf + off, (size_t)(n - off));
        if (w <= 0) return;                     /* full: drop, never block */
        off += (int)w;
    }
}

/* ── Key parsing ────────────────────────────────────────────────────────── */

static void ky_parse_one(ky_keys *k, const uint8_t *ks, int klen,
                         const uint8_t *vs, int vlen)
{
    long v = 0;

    if (klen != 1) return;                      /* all kitty keys are 1 char */

    switch (ks[0]) {
    case 'a':
        if (vlen == 1) { k->have_a = 1; k->act = vs[0]; }
        else k->parse_err = 1;
        return;
    case 'd':
        if (vlen == 1) { k->have_d = 1; k->dact = vs[0]; }
        else k->parse_err = 1;
        return;
    case 'f': if (ky_atol(vs, vlen, &v) == 0) { k->have_f = 1; k->f = v; } else k->parse_err = 1; return;
    case 'i': if (ky_atol(vs, vlen, &v) == 0) { k->have_i = 1; k->i = v; } else k->parse_err = 1; return;
    case 'm': if (ky_atol(vs, vlen, &v) == 0) { k->have_m = 1; k->m = v; } else k->parse_err = 1; return;
    case 'q': if (ky_atol(vs, vlen, &v) == 0) { k->have_q = 1; k->q = v; } else k->parse_err = 1; return;
    case 'c': if (ky_atol(vs, vlen, &v) == 0) { k->have_c = 1; k->c = v; } else k->parse_err = 1; return;
    case 'r': if (ky_atol(vs, vlen, &v) == 0) { k->have_r = 1; k->r = v; } else k->parse_err = 1; return;
    case 'x': if (ky_atol(vs, vlen, &v) == 0) { k->have_x = 1; k->x = v; } else k->parse_err = 1; return;
    case 'y': if (ky_atol(vs, vlen, &v) == 0) { k->have_y = 1; k->y = v; } else k->parse_err = 1; return;
    case 'w': if (ky_atol(vs, vlen, &v) == 0) { k->have_w = 1; k->w = v; } else k->parse_err = 1; return;
    case 'h': if (ky_atol(vs, vlen, &v) == 0) { k->have_h = 1; k->h = v; } else k->parse_err = 1; return;
    case 'X': if (ky_atol(vs, vlen, &v) == 0) { k->have_X = 1; k->X = v; } else k->parse_err = 1; return;
    case 'Y': if (ky_atol(vs, vlen, &v) == 0) { k->have_Y = 1; k->Y = v; } else k->parse_err = 1; return;
    case 'z': if (ky_atol(vs, vlen, &v) == 0) { k->have_z = 1; k->z = v; } else k->parse_err = 1; return;
    case 'U': if (ky_atol(vs, vlen, &v) == 0) { k->have_U = 1; k->U = v; } else k->parse_err = 1; return;
    case 'p': if (ky_atol(vs, vlen, &v) == 0) { k->have_p = 1; k->p = v; } else k->parse_err = 1; return;
    case 't': k->have_t = 1; return;
    case 'o': k->have_o = 1; return;
    default:  return;                           /* unknown keys: forward
                                                   compatibility, ignored */
    }
}

static void ky_parse(ky_keys *k, const uint8_t *payload, int len)
{
    int semi = -1, end, pos = 1, i;

    memset(k, 0, sizeof *k);
    for (i = 1; i < len; i++)
        if (payload[i] == ';') { semi = i; break; }
    if (semi >= 0) {
        k->data = payload + semi + 1;
        k->dlen = len - semi - 1;
    }
    end = (semi >= 0) ? semi : len;

    while (pos < end) {
        int ks = pos, ke = -1, vs, ve;
        while (pos < end && payload[pos] != ',') {
            if (payload[pos] == '=' && ke < 0) ke = pos;
            pos++;
        }
        if (ke < 0) {                            /* bare token: skip it */
            if (pos < end) pos++;
            continue;
        }
        vs = ke + 1;
        ve = pos;
        ky_parse_one(k, payload + ks, ke - ks, payload + vs, ve - vs);
        if (pos < end) pos++;                    /* past the ',' */
    }
}

/* ── base64 → binary, streamed across chunks ────────────────────────────── */

static void ky_asm_free(void)
{
    free(g_asm);
    g_asm = NULL;
    g_asm_cap = g_asm_len = 0;
    g_asm_on = 0;
    g_asm_have_f = g_asm_have_w = g_asm_have_h = g_asm_have_i = 0;
    g_asm_act = 0;
    g_b64_bits = 0;
    g_b64_n = 0;
}

/* Feed one chunk's base64 into the assembly. Returns 0, or -1 on invalid
 * base64 or the size cap — the assembly is dropped immediately so a
 * poisoned stream cannot leak into the next transmission. */
static int ky_asm_append(const uint8_t *s, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        int c = s[i], v;

        if (c == '\r' || c == '\n') continue;
        if (c == '=') {                          /* quantum ends here */
            g_b64_bits = 0;
            g_b64_n = 0;
            continue;
        }
        v = b64_val(c);
        if (v < 0) return -1;
        g_b64_bits = (g_b64_bits << 6) | (unsigned)v;
        g_b64_n += 6;
        while (g_b64_n >= 8) {
            if (g_asm_len + 1 > KY_MAX_BINARY) return -1;
            if (g_asm_len == g_asm_cap) {
                size_t ncap = g_asm_cap ? g_asm_cap * 2 : 65536;
                uint8_t *np;
                if (ncap > KY_MAX_BINARY) ncap = KY_MAX_BINARY;
                np = (uint8_t *)realloc(g_asm, ncap);
                if (!np) return -1;
                g_asm = np;
                g_asm_cap = ncap;
            }
            g_asm[g_asm_len++] = (uint8_t)(g_b64_bits >> (g_b64_n - 8));
            g_b64_n -= 8;
        }
    }
    return 0;
}

/* Length of the decoded stream without storing it (query validation). */
static long ky_b64_drylen(const uint8_t *s, int len, int *bad)
{
    long chars = 0;
    int i;

    *bad = 0;
    for (i = 0; i < len; i++) {
        int c = s[i];
        if (c == '\r' || c == '\n' || c == '=') continue;
        if (b64_val(c) < 0) { *bad = 1; return -1; }
        chars++;
    }
    return chars * 6 / 8;
}

/* ── Image slots ────────────────────────────────────────────────────────── */

static ky_slot *ky_slot_get(uint32_t id)
{
    int i, free_i = -1, evict = -1;

    for (i = 0; i < KY_SLOTS; i++)
        if (g_slots[i].used && g_slots[i].id == id) return &g_slots[i];
    for (i = 0; i < KY_SLOTS; i++)
        if (!g_slots[i].used) { free_i = i; break; }
    if (free_i < 0) {                            /* table full: coldest out */
        for (i = 0; i < KY_SLOTS; i++)
            if (evict < 0 || g_slots[i].seq < g_slots[evict].seq) evict = i;
        free_i = evict;
    }
    {
        ky_slot *s = &g_slots[free_i];
        free(s->px);
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->id = id;
        return s;
    }
}

static ky_slot *ky_slot_find(uint32_t id)
{
    int i;
    for (i = 0; i < KY_SLOTS; i++)
        if (g_slots[i].used && g_slots[i].id == id) return &g_slots[i];
    return NULL;
}

/* ── Decode ─────────────────────────────────────────────────────────────── */

static int ky_dims_ok(long w, long h)
{
    return w >= 1 && h >= 1 && w <= KY_MAX_DIM && h <= KY_MAX_DIM &&
           w * h <= KY_MAX_PIXELS;
}

/* Decode one complete payload into a fresh ARGB buffer.
 * Returns "OK" or an error code string; out/w/h are set only on OK. */
static const char *ky_decode(const ky_keys *k, const uint8_t *data, int dlen,
                             uint32_t **out, int *w, int *h)
{
    long f = k->have_f ? k->f : -1;
    long dw = k->have_w ? k->w : -1;
    long dh = k->have_h ? k->h : -1;

    /* Data-carrying raw frames with x/y would be a partial transmit — cut. */
    if ((f == 24 || f == 32) &&
        ((k->have_x && k->x != 0) || (k->have_y && k->y != 0)))
        return "ENOTSUPPORTED";

    if (f == 100) {
        int iw = 0, ih = 0, n = 0, i;
        long pw, ph;
        unsigned char *rgba;
        uint32_t *px;

        if (dlen < 24) return "EINVAL";
        /* IHDR pre-check before stb allocates anything: a small hostile
         * PNG can declare gigapixel dimensions. */
        if (data[0] != 0x89 || data[1] != 'P' || data[2] != 'N' ||
            data[3] != 'G' || data[4] != 0x0D || data[5] != 0x0A ||
            data[6] != 0x1A || data[7] != 0x0A)
            return "EINVAL";
        if (memcmp(data + 12, "IHDR", 4) != 0) return "EINVAL";
        pw = ((long)data[16] << 24) | ((long)data[17] << 16) |
             ((long)data[18] << 8)  |  (long)data[19];
        ph = ((long)data[20] << 24) | ((long)data[21] << 16) |
             ((long)data[22] << 8)  |  (long)data[23];
        if (!ky_dims_ok(pw, ph)) return "EINVAL";

        rgba = stbi_load_from_memory(data, dlen, &iw, &ih, &n, 4);
        if (!rgba || iw != (int)pw || ih != (int)ph) {
            free(rgba);
            return "EINVAL";
        }
        px = (uint32_t *)malloc((size_t)iw * ih * 4);
        if (!px) { free(rgba); return "ENOMEM"; }
        for (i = 0; i < iw * ih; i++)
            px[i] = ((uint32_t)rgba[i * 4 + 3] << 24) |
                    ((uint32_t)rgba[i * 4] << 16) |
                    ((uint32_t)rgba[i * 4 + 1] << 8) |
                    (uint32_t)rgba[i * 4 + 2];
        free(rgba);
        *out = px;
        *w = iw;
        *h = ih;
        return "OK";
    }

    if (f == 32 || f == 24) {
        int bpp = (f == 32) ? 4 : 3;
        uint32_t *px;
        long i;

        if (!k->have_w || !k->have_h || !ky_dims_ok(dw, dh))
            return "EINVAL";
        if (dlen != dw * dh * bpp) return "EINVAL";
        px = (uint32_t *)malloc((size_t)dw * dh * 4);
        if (!px) return "ENOMEM";
        for (i = 0; i < dw * dh; i++) {
            uint32_t r = data[i * bpp], g = data[i * bpp + 1],
                     b = data[i * bpp + 2];
            uint32_t a = (bpp == 4) ? data[i * bpp + 3] : 0xFFu;
            px[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        *out = px;
        *w = (int)dw;
        *h = (int)dh;
        return "OK";
    }

    return "ENOTSUPPORTED";
}

/* ── Placement ──────────────────────────────────────────────────────────── */

/* Resolve a placement for image dimensions img_w×img_h. `crop_from_keys`
 * is 0 on data-carrying frames (their x/y/w/h are data dims, not crop —
 * the whole image is displayed) and 1 on placement-only frames (a=p and
 * a=T without data, where x/y/w/h select the source rectangle).
 * Returns "OK" or an error code string. On OK the slot is fully placed. */
static const char *ky_place(ky_slot *s, const ky_keys *k, long img_w,
                            long img_h, const tgs_term *term,
                            int cell_w, int cell_h, int crop_from_keys)
{
    long sx = 0, sy = 0, sw = img_w, sh = img_h;
    long dw, dh;
    int cx, cy;

    if (cell_w < 1 || cell_h < 1) return "EINVAL";

    if (crop_from_keys) {
        sx = k->have_x ? k->x : 0;
        sy = k->have_y ? k->y : 0;
        if (sx < 0 || sy < 0) return "EINVAL";
        if (k->have_w && k->w > 0) sw = k->w;
        if (k->have_h && k->h > 0) sh = k->h;
        if (sw <= 0 || sh <= 0 || sx + sw > img_w || sy + sh > img_h)
            return "EINVAL";
    }

    /* c/r are the display SIZE in cells (not coordinates): the placement
     * position is the cursor cell — there is no position key at all. */
    if (k->have_c && k->c > 0 && k->have_r && k->r > 0) {
        dw = k->c * cell_w;
        dh = k->r * cell_h;
    } else if (k->have_c && k->c > 0) {
        dw = k->c * cell_w;
        dh = (sh * dw + sw / 2) / sw;            /* aspect-derived rows */
        if (dh < 1) dh = 1;
    } else if (k->have_r && k->r > 0) {
        dh = k->r * cell_h;
        dw = (sw * dh + sh / 2) / sh;
        if (dw < 1) dw = 1;
    } else {
        dw = sw;
        dh = sh;
    }

    cx = term ? tgs_term_cx(term) : 0;
    cy = term ? tgs_term_cy(term) : 0;

    s->x0 = cx * cell_w + (k->have_X ? (int)k->X : 0);
    s->y0 = cy * cell_h + (k->have_Y ? (int)k->Y : 0);
    s->dw = (int)dw;
    s->dh = (int)dh;
    s->sx = (int)sx;
    s->sy = (int)sy;
    s->sw = (int)sw;
    s->sh = (int)sh;
    s->z = k->have_z ? (int)k->z : 0;
    s->seq = ++g_seq;
    s->placed = 1;
    return "OK";
}

/* ── Actions ────────────────────────────────────────────────────────────── */

static const char *ky_do_delete(const ky_keys *k, const tgs_term *term,
                                int cell_w, int cell_h)
{
    int scope = k->have_d ? k->dact : 'c';      /* default: cursor cell */
    int i;

    switch (scope) {
    case 'a':
        for (i = 0; i < KY_SLOTS; i++) {
            free(g_slots[i].px);
            memset(&g_slots[i], 0, sizeof g_slots[i]);
        }
        return "OK";
    case 'p':
        for (i = 0; i < KY_SLOTS; i++) g_slots[i].placed = 0;
        return "OK";
    case 'i':
        if (!k->have_i) return "EINVAL";
        {
            ky_slot *s = ky_slot_find((uint32_t)k->i);
            if (s) { free(s->px); memset(s, 0, sizeof *s); }
        }
        return "OK";                             /* idempotent: missing = OK */
    case 'c': {
        /* Cell range x,y,w,h (default: the cursor cell, extent 1×1);
         * placements whose span intersects it are cleared. */
        long rx, ry, rw, rh;
        long c0, r0, c1, r1;
        int cx = term ? tgs_term_cx(term) : 0;
        int cy = term ? tgs_term_cy(term) : 0;

        rx = k->have_x ? k->x : cx;
        ry = k->have_y ? k->y : cy;
        rw = (k->have_w && k->w > 0) ? k->w : 1;
        rh = (k->have_h && k->h > 0) ? k->h : 1;
        if (rx < 0 || ry < 0) return "EINVAL";
        c0 = rx;
        r0 = ry;
        c1 = rx + rw;
        r1 = ry + rh;
        for (i = 0; i < KY_SLOTS; i++) {
            ky_slot *s = &g_slots[i];
            long sc0, sr0, sc1, sr1;
            if (!s->used || !s->placed) continue;
            sc0 = s->x0 / cell_w;
            sr0 = s->y0 / cell_h;
            sc1 = (s->x0 + s->dw + cell_w - 1) / cell_w;
            sr1 = (s->y0 + s->dh + cell_h - 1) / cell_h;
            if (sc0 < c1 && c0 < sc1 && sr0 < r1 && r0 < sr1)
                s->placed = 0;
        }
        return "OK";
    }
    default:
        return "ENOTSUPPORTED";
    }
}

static const char *ky_do_query(const ky_keys *k)
{
    long f = k->have_f ? k->f : -1;
    int bad = 0;
    long dlen;

    if (!k->data || k->dlen <= 0) return "EINVAL";
    dlen = ky_b64_drylen(k->data, k->dlen, &bad);
    if (bad) return "EINVAL";

    if (f == 100) return "OK";                   /* no PNG decode, per spec */
    if (f == 32 || f == 24) {
        long w = k->have_w ? k->w : -1, h = k->have_h ? k->h : -1;
        int bpp = (f == 32) ? 4 : 3;
        if (!ky_dims_ok(w, h) || dlen != w * h * bpp) return "EINVAL";
        return "OK";
    }
    return "ENOTSUPPORTED";
}

/* ── Feed ───────────────────────────────────────────────────────────────── */

void tgs_kitty_feed(const uint8_t *payload, int len, const tgs_term *term,
                    int cell_w, int cell_h, int reply_fd)
{
    ky_keys k;
    int eff_act, is_final, has_data;
    uint32_t id;
    const char *err;

    if (!payload || len < 2 || payload[0] != 'G') return;
    ky_parse(&k, payload, len);

    if (k.parse_err) { ky_reply(reply_fd, &k, "EINVAL"); return; }
    if (k.have_t || k.have_o) {                  /* file/shm targets: cut */
        ky_reply(reply_fd, &k, "ENOTSUPPORTED");
        return;
    }
    if (k.have_U && k.U != 0) {                  /* unicode-placeholder
                                                    virtual placement: cut */
        ky_reply(reply_fd, &k, "ENOTSUPPORTED");
        return;
    }

    has_data = (k.dlen > 0);
    is_final = !(k.have_m && k.m == 1);

    /* Effective action: lenient chunkers may drop a= on later chunks —
     * the assembly remembers it. */
    eff_act = k.have_a ? k.act : (g_asm_on ? g_asm_act : 0);

    /* ── Transmission path (a=t/T carrying data, or finishing assembly) ── */
    if ((eff_act == 't' || eff_act == 'T') && (has_data || g_asm_on)) {
        if (k.have_a) g_asm_act = eff_act;
        if (k.have_f) { g_asm_have_f = 1; g_asm_f = k.f; }
        if (k.have_w) { g_asm_have_w = 1; g_asm_w = k.w; }
        if (k.have_h) { g_asm_have_h = 1; g_asm_h = k.h; }
        if (k.have_i) { g_asm_have_i = 1; g_asm_i = k.i; }

        if (has_data && ky_asm_append(k.data, k.dlen) < 0) {
            ky_reply(reply_fd, &k, "EINVAL");
            ky_asm_free();
            return;
        }
        if (!is_final) {                         /* wait for more chunks */
            g_asm_on = 1;
            return;
        }
        if (g_b64_n == 6) {                      /* stream ends one char
                                                    short of a quantum */
            ky_reply(reply_fd, &k, "EINVAL");
            ky_asm_free();
            return;
        }

        /* Final chunk: merge dims/identity the frame may have omitted. */
        if (!k.have_f && g_asm_have_f) { k.have_f = 1; k.f = g_asm_f; }
        if (!k.have_w && g_asm_have_w) { k.have_w = 1; k.w = g_asm_w; }
        if (!k.have_h && g_asm_have_h) { k.have_h = 1; k.h = g_asm_h; }
        if (!k.have_i && g_asm_have_i) { k.have_i = 1; k.i = g_asm_i; }

        if (k.have_p && k.p > 0) {               /* multi-placement: cut */
            ky_reply(reply_fd, &k, "ENOTSUPPORTED");
            ky_asm_free();
            return;
        }

        {
            uint32_t *ipx = NULL;
            int iw = 0, ih = 0;
            ky_slot *s;

            err = ky_decode(&k, g_asm ? g_asm : (const uint8_t *)"",
                            (int)g_asm_len, &ipx, &iw, &ih);
            ky_asm_free();
            if (strcmp(err, "OK") != 0) { ky_reply(reply_fd, &k, err); return; }

            /* Identity: explicit id, else an auto id parked far from the
             * range apps allocate from (i=0 is not a valid id). */
            if (k.have_i && k.i > 0) {
                id = (uint32_t)k.i;
            } else {
                if (k.have_i) k.have_i = 0;      /* i=0 → auto, hence no ack */
                id = g_next_auto++;
                if (g_next_auto > 0x7FFFFFFFu) g_next_auto = KY_AUTO_ID_BASE;
            }

            s = ky_slot_get(id);
            free(s->px);                         /* replace: old data and the
                                                    placement over it both go */
            s->px = ipx;
            s->w = iw;
            s->h = ih;
            s->placed = 0;
            g_dirty = 1;                         /* visible layer may change */

            if (eff_act == 'T') {
                /* Data frame: full image (their x/y/w/h are data dims). */
                err = ky_place(s, &k, iw, ih, term, cell_w, cell_h, 0);
                if (strcmp(err, "OK") != 0) { ky_reply(reply_fd, &k, err); return; }
                g_dirty = 1;
            }
            ky_reply(reply_fd, &k, "OK");
        }
        return;
    }

    /* ── No-data commands ──────────────────────────────────────────────── */
    switch (eff_act) {
    case 'T':                                    /* display-only (no data) */
    case 'p':
        if (has_data) { ky_reply(reply_fd, &k, "EINVAL"); return; }
        if (!k.have_i) { ky_reply(reply_fd, &k, "EINVAL"); return; }
        if (k.have_p && k.p > 0) { ky_reply(reply_fd, &k, "ENOTSUPPORTED"); return; }
        {
            ky_slot *s = ky_slot_find((uint32_t)k.i);
            if (!s || !s->px) { ky_reply(reply_fd, &k, "ENOENT"); return; }
            err = ky_place(s, &k, s->w, s->h, term, cell_w, cell_h, 1);
            if (strcmp(err, "OK") != 0) { ky_reply(reply_fd, &k, err); return; }
            g_dirty = 1;
            ky_reply(reply_fd, &k, "OK");
        }
        return;
    case 't':
        ky_reply(reply_fd, &k, "EINVAL");        /* transmit without data */
        return;
    case 'd':
        if (has_data) { ky_reply(reply_fd, &k, "EINVAL"); return; }
        err = ky_do_delete(&k, term, cell_w, cell_h);
        if (strcmp(err, "OK") == 0) g_dirty = 1;
        ky_reply(reply_fd, &k, err);
        return;
    case 'q':
        ky_reply(reply_fd, &k, ky_do_query(&k));
        return;
    default:
        /* a= is required by the protocol; a missing one is malformed, an
         * unknown letter (animation actions a=/f=/c= …) is out of scope. */
        ky_reply(reply_fd, &k, k.have_a ? "ENOTSUPPORTED" : "EINVAL");
        return;
    }
}

/* ── Draw / state ───────────────────────────────────────────────────────── */

int tgs_kitty_take_dirty(void)
{
    int d = g_dirty;
    g_dirty = 0;
    return d;
}

void tgs_kitty_mark_dirty(void)
{
    g_dirty = 1;
}

void tgs_kitty_draw(uint32_t *px, int px_w, int px_h)
{
    int idx[KY_SLOTS], n = 0, i, j;

    if (!px || px_w <= 0 || px_h <= 0) return;
    for (i = 0; i < KY_SLOTS; i++)
        if (g_slots[i].used && g_slots[i].placed && g_slots[i].px)
            idx[n++] = i;

    /* Ascending (z, placement order) — kitty's z-index with insertion as
     * the tie-break, over a table of at most 64. */
    for (i = 1; i < n; i++) {
        int t = idx[i];
        j = i - 1;
        while (j >= 0 && (g_slots[idx[j]].z > g_slots[t].z ||
               (g_slots[idx[j]].z == g_slots[t].z &&
                g_slots[idx[j]].seq > g_slots[t].seq))) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = t;
    }

    for (i = 0; i < n; i++) {
        const ky_slot *s = &g_slots[idx[i]];
        int dx, dy;

        for (dy = 0; dy < s->dh; dy++) {
            int ty = s->y0 + dy;
            long sy;
            if (ty < 0) continue;
            if (ty >= px_h) break;
            sy = (long)s->sy + (long)dy * s->sh / s->dh;
            for (dx = 0; dx < s->dw; dx++) {
                int tx = s->x0 + dx;
                long sx;
                uint32_t sp, a, d;
                if (tx < 0) continue;
                if (tx >= px_w) break;
                sx = (long)s->sx + (long)dx * s->sw / s->dw;
                sp = s->px[sy * s->w + sx];
                a = sp >> 24;
                if (a == 0) continue;
                d = px[ty * px_w + tx];
                if (a == 255) {
                    px[ty * px_w + tx] = sp;
                } else {                         /* source-over, same /255
                                                    contract as the glyphs */
                    uint32_t ia = 255 - a;
                    uint32_t r = ((sp >> 16 & 0xFF) * a + (d >> 16 & 0xFF) * ia) / 255;
                    uint32_t g = ((sp >> 8 & 0xFF) * a + (d >> 8 & 0xFF) * ia) / 255;
                    uint32_t b = ((sp & 0xFF) * a + (d & 0xFF) * ia) / 255;
                    px[ty * px_w + tx] = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}

void tgs_kitty_reset(void)
{
    int i;
    for (i = 0; i < KY_SLOTS; i++) {
        free(g_slots[i].px);
        memset(&g_slots[i], 0, sizeof g_slots[i]);
    }
    ky_asm_free();
    g_seq = 0;
    g_next_auto = KY_AUTO_ID_BASE;
    g_dirty = 1;
}
