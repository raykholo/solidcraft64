/* viewdemo.c - test harness for the wireframe (lane A). Not part of the program.
 *
 *   make viewdemo        ->  build/viewdemo.prg
 *
 * Keys: A D yaw, W S pitch, T line self-test, N 10-feature part (default), M 14-feature part,
 *       Q quit. Every redraw prints how many cycles it took.
 */
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "../src/model.h"
#include "../src/line.h"

#define PEEK(a) (*(volatile unsigned char *)(a))
#define POKE(a, v) (*(volatile unsigned char *)(a) = (v))

static void add(unsigned char type, unsigned char ref, int a, int b, int c, int d)
{
    feature f;
    f.type = type; f.ref = ref; f.a = a; f.b = b; f.c = c; f.d = d;
    model_add(&f);
}

/* plate 80 x 50 x 8, a 30 mm boss 10 high on top, a 10 mm hole right through,
 * and (with big != 0) four more holes and a pocket */
static void build(unsigned char big)
{
    model_new();
    add(F_RECT, 0xFF, 0, 0, 800, 500);      /* 0 */
    add(F_EXTRUDE, 0, 80, 0, 0, 0);         /* 1 plate */
    add(F_CIRCLE, 0xFF, 400, 250, 300, 0);  /* 2 */
    add(F_EXTRUDE, 2, 100, 80, 0, 0);       /* 3 boss */
    add(F_CIRCLE, 0xFF, 400, 250, 100, 0);  /* 4 */
    add(F_CUT, 4, 180, 180, 0, 0);          /* 5 hole through boss and plate */
    add(F_CIRCLE, 0xFF, 100, 100, 60, 0);   /* 6 */
    add(F_CUT, 6, 80, 80, 0, 0);            /* 7 */
    add(F_CIRCLE, 0xFF, 700, 400, 60, 0);   /* 8 */
    add(F_CUT, 8, 80, 80, 0, 0);            /* 9 */
    if (big) {
        add(F_CIRCLE, 0xFF, 700, 100, 60, 0);  /* 10 */
        add(F_CUT, 10, 80, 80, 0, 0);          /* 11 */
        add(F_RECT, 0xFF, 80, 330, 140, 100);  /* 12 */
        add(F_CUT, 12, 40, 80, 0, 0);          /* 13 pocket */
    }
}

/* ---- timing: CIA2 timers A and B cascaded, counting machine cycles ---- */
static void timer_start(void)
{
    POKE(0xDD0E, 0); POKE(0xDD0F, 0);
    POKE(0xDD04, 0xFF); POKE(0xDD05, 0xFF);
    POKE(0xDD06, 0xFF); POKE(0xDD07, 0xFF);
    POKE(0xDD0F, 0x51);
    POKE(0xDD0E, 0x11);
}

static unsigned long timer_stop(void)
{
    unsigned int a, b;
    POKE(0xDD0E, 0); POKE(0xDD0F, 0);
    a = PEEK(0xDD04) | ((unsigned)PEEK(0xDD05) << 8);
    b = PEEK(0xDD06) | ((unsigned)PEEK(0xDD07) << 8);
    return (unsigned long)(0xFFFFu - b) * 65536UL + (0xFFFFu - a);
}

static void show(const char *s, unsigned char row) { view_text(0, row, s); }

static void redraw(void)
{
    char buf[40];
    unsigned long c;
    timer_start();
    view_draw();
    c = timer_stop();
    sprintf(buf, "%lu CYCLES %lu MS", c, c * 1000UL / 985248UL);
    show(buf, 0);
}

/* ---- line self-test ---- */
static unsigned char bitat(int x, int y)
{
    unsigned char save = PEEK(1), v;
    unsigned char *p = (unsigned char *)(0xE000u + (unsigned)(y >> 3) * 320u + (unsigned)(y & 7) + (unsigned)(x & ~7));
    asm("sei");
    POKE(1, 0x35);
    v = *p;
    POKE(1, save);
    asm("cli");
    return (v >> (7 - (x & 7))) & 1;
}

static unsigned int popcount_box(int xa, int ya, int xb, int yb)
{
    unsigned int n = 0, x, y;
    unsigned char save = PEEK(1), v;
    unsigned char *p;
    asm("sei");
    POKE(1, 0x35);
    for (y = ya; y <= (unsigned)yb; ++y)
        for (x = xa & ~7; x <= (unsigned)xb; x += 8) {
            p = (unsigned char *)(0xE000u + (y >> 3) * 320u + (y & 7) + x);
            v = *p;
            while (v) { v &= v - 1; ++n; }
        }
    POKE(1, save);
    asm("cli");
    return n;
}

static unsigned long seed = 12345;
static int rnd(int n) { seed = seed * 1103515245UL + 12345UL; return (int)((seed >> 16) % (unsigned)n); }

/* the same algorithm as line.s, written the long way round */
static unsigned int refline(int x0, int y0, int x1, int y1, unsigned char check)
{
    int dx = x1 - x0, dy = y1 - y0, adx, ady, err, sx, sy, n, i, t;
    unsigned int count = 0;

    adx = dx < 0 ? -dx : dx; ady = dy < 0 ? -dy : dy;
    if (adx >= ady) {
        if (dx < 0) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dy = -dy; }
        sy = dy < 0 ? -1 : 1; n = adx; err = adx / 2;
        for (i = 0; ; ++i) {
            if (check && !bitat(x0, y0)) return 0xFFFF;
            ++count;
            if (i == n) break;
            ++x0; err -= ady;
            if (err < 0) { err += adx; y0 += sy; }
        }
    } else {
        if (dy < 0) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dx = -dx; }
        sx = dx < 0 ? -1 : 1; n = ady; err = ady / 2;
        for (i = 0; ; ++i) {
            if (check && !bitat(x0, y0)) return 0xFFFF;
            ++count;
            if (i == n) break;
            ++y0; err -= adx;
            if (err < 0) { err += ady; x0 += sx; }
        }
    }
    return count;
}

static void selftest(void)
{
    int i, x0, y0, x1, y1, tmp;
    unsigned int want, got;
    char buf[40];

    line_clear();
    for (i = 0; i < 64; ++i) {
        if (i < 8) {   /* the edges and diagonals first */
            static const int e[8][4] = {
                {0,0,319,199}, {319,199,0,0}, {0,199,319,0}, {319,0,0,199},
                {0,100,319,100}, {160,0,160,199}, {5,5,5,5}, {319,199,0,199} };
            x0 = e[i][0]; y0 = e[i][1]; x1 = e[i][2]; y1 = e[i][3];
        } else {
            x0 = rnd(320); y0 = rnd(200); x1 = rnd(320); y1 = rnd(200);
            if (i & 1) { x1 = x0 + rnd(41) - 20; y1 = y0 + rnd(41) - 20; }   /* short ones too */
            if (x1 < 0) x1 = 0; if (x1 > 319) x1 = 319;
            if (y1 < 0) y1 = 0; if (y1 > 199) y1 = 199;
        }
        line_clear();
        line_x0 = x0; line_y0 = y0; line_x1 = x1; line_y1 = y1;
        line_draw();
        want = refline(x0, y0, x1, y1, 1);
        got = popcount_box(0, 0, 319, 199);          /* nothing outside the line either */
        tmp = (want == 0xFFFF) || (got != want);
        if (tmp) {
            sprintf(buf, "FAIL %d: %d,%d-%d,%d", i, x0, y0, x1, y1);
            show(buf, 0);
            sprintf(buf, "WANT %u GOT %u", want, got);
            show(buf, 1);
            return;
        }
    }
    sprintf(buf, "%d LINES OK", i);
    show(buf, 0);
}

int main(void)
{
    unsigned char k;

    build(0);
    view_init();
    redraw();
    for (;;) {
        k = cgetc();
        if (k == 0x51 || k == 0xD1) break;
        switch (k) {
        case 0x41: case 0xC1: view_rotate(-1, 0); break;   /* PETSCII: A is $41 or $C1 */
        case 0x44: case 0xC4: view_rotate(1, 0); break;
        case 0x57: case 0xD7: view_rotate(0, 1); break;
        case 0x53: case 0xD3: view_rotate(0, -1); break;
        case 0x4E: case 0xCE: build(0); break;
        case 0x4D: case 0xCD: build(1); break;
        case 0x54: case 0xD4: selftest(); continue;
        default: continue;
        }
        redraw();
    }
    view_done();
    return 0;
}
