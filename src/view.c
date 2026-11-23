/* view.c - the hi-res wireframe.
 *
 * VIC bank 3, bitmap at $E000, colour matrix at $C000 (docs/MEMORY.md).
 * One foreground colour and one background colour for the whole screen:
 * light blue on blue, which is what the machine does when you switch it on,
 * and so what 1982 looked like.
 *
 * Geometry is 8.8-ish fixed point: coordinates are scaled so the part fits
 * a circle of R_PX pixels, in eighths of a pixel ("q" units), then rotated by
 * yaw (about Z) and pitch (about X) with a sine table of 256 steps per turn,
 * orthographic. Circles are 24-gons; their projected points come from a
 * 24-entry unit ellipse worked out once per frame.
 */
#include <conio.h>
#include <string.h>
#include "model.h"
#include "line.h"

#define BITMAP   0xE000u
#define MATRIX   0xC000u
#define COLOURS  0xE6          /* light blue on blue */
#define BORDER   14

#define VIC_CTRL1 (*(volatile unsigned char *)0xD011)
#define VIC_MEM   (*(volatile unsigned char *)0xD018)
#define VIC_BORD  (*(volatile unsigned char *)0xD020)
#define CIA2_PRA  (*(volatile unsigned char *)0xDD00)

#define CX    176              /* where the middle of the part goes */
#define CY    98
#define R_PX  92               /* the part's bounding sphere, in pixels */
#define NSEG  24

/* sin(2 pi i / 256) * 127, generated once with tools/gensin.py */
static const signed char sintab[256] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
    90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92,
    90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54, 51,
    49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12, -9, -6, -3,
};
#define SIN(a) sintab[(unsigned char)(a)]
#define COS(a) sintab[(unsigned char)((a) + 64)]

static unsigned char yaw = 24, pitch = 22;      /* 1/256 turns */
static unsigned char sv_dd00, sv_d018, sv_d011, sv_d020, sv_d021;
static unsigned char started;

/* per frame */
static int cY, sY, cP, sP;
static unsigned int kk;                         /* q units per model unit = kk >> ksh */
static unsigned char ksh;
static int mcx, mcy, mcz;                       /* middle of the part, model units */
static int bx, by;                              /* where (0,0,0) lands on screen */
static int psx, psy;                            /* result of proj() */
static int ex[NSEG], ey[NSEG];                  /* unit circle, projected, about +-127 */
static unsigned char segang[NSEG];
static unsigned char used[MAX_FEATURES];

/* ------------------------------------------------------------------------ */
static void ln(int x0, int y0, int x1, int y1)
{
    /* nearly every line is wholly on screen, and clip_line is slow */
    if ((unsigned)x0 >= 320 || (unsigned)x1 >= 320 || (unsigned)y0 >= 200 || (unsigned)y1 >= 200)
        if (!clip_line(&x0, &y0, &x1, &y1)) return;
    line_x0 = x0; line_y0 = (unsigned char)y0;
    line_x1 = x1; line_y1 = (unsigned char)y1;
    line_draw();
}

/* q offsets from the middle of the part -> screen pixels */
static int mq(int a, int b) { mq_a = a; mq_b = b; return mulq(); }
static int mk(int a, unsigned int k, unsigned char sh) { mq_a = a; mk_k = k; mk_sh = sh; return mulk(); }

static void proj(int qx, int qy, int qz)
{
    int xr = mq(qx, cY) - mq(qy, sY);
    int yr = mq(qx, sY) + mq(qy, cY);
    int up = mq(qz, cP) + mq(yr, sP);
    psx = bx + ((xr + 4) >> 3);
    psy = by - ((up + 4) >> 3);
}

static int sq(int v, int mid) { return mk(v - mid, kk, ksh); }

static unsigned int isqrt32(unsigned long v)
{
    unsigned long r = 0, b = 0x40000000UL;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (unsigned int)r;
}

/* ------------------------------------------------------------------------ */
/* what the solid features span in z */
static void zrange(const feature *f, int *lo, int *hi)
{
    if (f->type == F_EXTRUDE) { *lo = f->b; *hi = f->b + f->a; }
    else                      { *lo = f->b - f->a; *hi = f->b; }
}

static unsigned char is_sketch(unsigned char t) { return t == F_RECT || t == F_CIRCLE; }

static unsigned char solid_sketch(const feature *f)
{
    return (f->type == F_EXTRUDE || f->type == F_CUT)
        && f->ref < g_part.count && is_sketch(g_part.f[f->ref].type);
}

static void extents(const feature *s, int *x0, int *y0, int *x1, int *y1)
{
    if (s->type == F_RECT) {
        *x0 = s->a; *y0 = s->b; *x1 = s->a + s->c; *y1 = s->b + s->d;
    } else {
        int r = s->c / 2;
        *x0 = s->a - r; *y0 = s->b - r; *x1 = s->a + r; *y1 = s->b + r;
    }
}

static void fit(void)
{
    unsigned char i;
    int x0, y0, x1, y1, lo, hi;
    int minx = 32767, miny = 32767, minz = 32767;
    int maxx = -32767, maxy = -32767, maxz = -32767;
    long dx, dy, dz, r;
    unsigned long k;
    unsigned int rm;

    memset(used, 0, sizeof used);
    for (i = 0; i < g_part.count; ++i)
        if (solid_sketch(&g_part.f[i])) used[g_part.f[i].ref] = 1;

    for (i = 0; i < g_part.count; ++i) {
        const feature *f = &g_part.f[i];
        if (is_sketch(f->type)) {
            extents(f, &x0, &y0, &x1, &y1);
            if (x0 < minx) minx = x0;
            if (y0 < miny) miny = y0;
            if (x1 > maxx) maxx = x1;
            if (y1 > maxy) maxy = y1;
            if (!used[i]) { if (minz > 0) minz = 0; if (maxz < 0) maxz = 0; }
        } else if (solid_sketch(f)) {
            zrange(f, &lo, &hi);
            if (lo < minz) minz = lo;
            if (hi > maxz) maxz = hi;
        }
    }
    if (minx > maxx) { minx = miny = -500; maxx = maxy = 500; }
    if (minz > maxz) { minz = maxz = 0; }

    mcx = (int)(((long)minx + maxx) / 2);
    mcy = (int)(((long)miny + maxy) / 2);
    mcz = (int)(((long)minz + maxz) / 2);
    dx = (long)maxx - minx; dy = (long)maxy - miny; dz = (long)maxz - minz;
    r = dx * dx + dy * dy + dz * dz;
    rm = (isqrt32((unsigned long)r) + 1) / 2;
    if (rm < 5) rm = 5;
    k = R_PX * 8UL * 65536UL / rm;                  /* q per model unit, 16.16 */
    ksh = 16;
    while (k >= 65536UL) { k >>= 1; --ksh; }
    kk = (unsigned int)k;
}

static void setup(void)
{
    unsigned char i;
    cY = COS(yaw); sY = SIN(yaw);
    cP = COS(pitch); sP = SIN(pitch);
    bx = CX; by = CY;
    for (i = 0; i < NSEG / 2; ++i) {        /* the other half are the same, negated */
        int c = COS(segang[i]), s = SIN(segang[i]);
        ex[i] = mq(c, cY) - mq(s, sY);
        ey[i] = mq(mq(c, sY) + mq(s, cY), sP);
        ex[i + NSEG / 2] = -ex[i];
        ey[i + NSEG / 2] = -ey[i];
    }
}

/* ------------------------------------------------------------------------ */
static int ox[NSEG], oy[NSEG];

/* a sketch as a solid from z lo to z hi (lo == hi: just the outline) */
static void solid(const feature *s, int lo, int hi)
{
    unsigned char i, j;

    if (s->type == F_RECT) {
        int sx[8], sy[8];
        int x0 = sq(s->a, mcx), x1 = sq(s->a + s->c, mcx);
        int y0 = sq(s->b, mcy), y1 = sq(s->b + s->d, mcy);
        int z0 = sq(lo, mcz), z1 = sq(hi, mcz);

        proj(x0, y0, z0); sx[0] = psx; sy[0] = psy;
        proj(x1, y0, z0); sx[1] = psx; sy[1] = psy;
        proj(x1, y1, z0); sx[2] = psx; sy[2] = psy;
        proj(x0, y1, z0); sx[3] = psx; sy[3] = psy;
        for (i = 0; i < 4; ++i) { j = (i + 1) & 3; ln(sx[i], sy[i], sx[j], sy[j]); }
        if (lo == hi) return;
        proj(x0, y0, z1); sx[4] = psx; sy[4] = psy;
        proj(x1, y0, z1); sx[5] = psx; sy[5] = psy;
        proj(x1, y1, z1); sx[6] = psx; sy[6] = psy;
        proj(x0, y1, z1); sx[7] = psx; sy[7] = psy;
        for (i = 0; i < 4; ++i) {
            j = (i + 1) & 3;
            ln(sx[4 + i], sy[4 + i], sx[4 + j], sy[4 + j]);
            ln(sx[i], sy[i], sx[4 + i], sy[4 + i]);
        }
    } else {
        int cx = sq(s->a, mcx), cy = sq(s->b, mcy);
        int rq = mk(s->c, kk, ksh + 1);
        int sx0, sy0, sx1, sy1;
        unsigned char best = 0;
        int m = -1000;

        for (i = 0; i < NSEG / 2; ++i) {
            ox[i] = (mq(rq, ex[i]) + 4) >> 3;
            oy[i] = -((mq(rq, ey[i]) + 4) >> 3);
            ox[i + NSEG / 2] = -ox[i];
            oy[i + NSEG / 2] = -oy[i];
            if (ex[i] > m) { m = ex[i]; best = i; }
            if (-ex[i] > m) { m = -ex[i]; best = i + NSEG / 2; }
        }
        proj(cx, cy, sq(lo, mcz)); sx0 = psx; sy0 = psy;
        for (i = 0; i < NSEG; ++i) {
            j = (i + 1 == NSEG) ? 0 : i + 1;
            ln(sx0 + ox[i], sy0 + oy[i], sx0 + ox[j], sy0 + oy[j]);
        }
        if (lo == hi) return;
        proj(cx, cy, sq(hi, mcz)); sx1 = psx; sy1 = psy;
        for (i = 0; i < NSEG; ++i) {
            j = (i + 1 == NSEG) ? 0 : i + 1;
            ln(sx1 + ox[i], sy1 + oy[i], sx1 + ox[j], sy1 + oy[j]);
        }
        /* four verticals: the left/right silhouette and the two between */
        for (i = 0; i < 4; ++i) {
            j = best + i * (NSEG / 4);
            if (j >= NSEG) j -= NSEG;
            ln(sx0 + ox[j], sy0 + oy[j], sx1 + ox[j], sy1 + oy[j]);
        }
    }
}

/* ------------------------------------------------------------------------ */
static void label(int x, int y, const char *s)
{
    int c = (x - 4) / 8, r = (y - 4) / 8;
    if (c < 0) c = 0; if (c > 39) c = 39;
    if (r < 0) r = 0; if (r > 24) r = 24;
    view_text((unsigned char)c, (unsigned char)r, s);
}

/* the origin triad, bottom left, like SolidCraft 2000's */
static void triad(void)
{
    static const char names[3][2] = { "X", "Y", "Z" };
    int ox0 = 30, oy0 = 168, i;
    int tx[3], ty[3];

    bx = 0; by = 0;
    proj(160, 0, 0);   tx[0] = psx; ty[0] = psy;
    proj(0, 160, 0);   tx[1] = psx; ty[1] = psy;
    proj(0, 0, 160);   tx[2] = psx; ty[2] = psy;
    for (i = 0; i < 3; ++i) {
        ln(ox0, oy0, ox0 + tx[i], oy0 + ty[i]);
        label(ox0 + tx[i] + tx[i] / 3, oy0 + ty[i] + ty[i] / 3, names[i]);
    }
    bx = CX; by = CY;
}

void view_draw(void)
{
    unsigned char i;
    const feature *f;

    line_clear();
    fit();
    setup();
    for (i = 0; i < g_part.count; ++i) {
        int lo, hi;
        f = &g_part.f[i];
        if (solid_sketch(f)) {
            zrange(f, &lo, &hi);
            solid(&g_part.f[f->ref], lo, hi);
        } else if (is_sketch(f->type) && !used[i]) {
            solid(f, 0, 0);
        }
    }
    triad();
}

void view_rotate(signed char dyaw, signed char dpitch)
{
    int p;
    yaw += (signed char)(dyaw * 4);
    p = (signed char)pitch + dpitch * 4;
    if (p > 64) p = 64;
    if (p < -64) p = -64;
    pitch = (unsigned char)p;
}

/* ------------------------------------------------------------------------ */
void view_text(unsigned char col, unsigned char row, const char *s)
{
    unsigned char *p = (unsigned char *)(BITMAP + (unsigned)row * 320u + (unsigned)col * 8u);
    unsigned char c, k;
    const unsigned char *g;

    for (; *s && col < 40; ++s, ++col, p += 8) {
        c = (unsigned char)*s;
        /* cc65 hands us PETSCII: A-Z are $C1-$DA, a-z are $41-$5A */
        if (c >= 0xC0 && c < 0xE0) c -= 0xC0;
        else if (c >= 0x40 && c < 0x60) c -= 0x40;
        if (c >= 64) c = 32;
        g = fontbuf + (unsigned)c * 8u;
        for (k = 0; k < 8; ++k) p[k] = g[k];
    }
}

void view_init(void)
{
    unsigned char i;
    unsigned int a;

    cursor(0);
    if (!started) {
        font_load();
        for (i = 0; i < NSEG; ++i) segang[i] = (unsigned char)((i * 32) / 3);
        started = 1;
    }
    for (i = 0; i < 200; ++i) {
        a = BITMAP + (unsigned)(i >> 3) * 320u + (i & 7);
        rowlo[i] = (unsigned char)a;
        rowhi[i] = (unsigned char)(a >> 8);
    }
    line_clear();
    memset((void *)MATRIX, COLOURS, 1000);

    sv_dd00 = CIA2_PRA; sv_d018 = VIC_MEM; sv_d011 = VIC_CTRL1;
    sv_d020 = VIC_BORD; sv_d021 = *(volatile unsigned char *)0xD021;
    CIA2_PRA = sv_dd00 & 0xFC;          /* bank 3: $C000-$FFFF */
    VIC_MEM = 0x08;                     /* matrix at +$0000, bitmap at +$2000 */
    VIC_CTRL1 = sv_d011 | 0x20;
    VIC_BORD = BORDER;
}

void view_done(void)
{
    CIA2_PRA = sv_dd00;
    VIC_MEM = sv_d018;
    VIC_CTRL1 = sv_d011;
    VIC_BORD = sv_d020;
    *(volatile unsigned char *)0xD021 = sv_d021;
    clrscr();
    cursor(1);
}
