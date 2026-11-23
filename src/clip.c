/* clip.c - Cohen-Sutherland line clipping to the 320 x 200 screen. */
#include "line.h"

#define W 320
#define H 200

static unsigned char outcode(int x, int y)
{
    unsigned char c = 0;
    if (x < 0) c = 1; else if (x >= W) c = 2;
    if (y < 0) c |= 4; else if (y >= H) c |= 8;
    return c;
}

unsigned char clip_line(int *px0, int *py0, int *px1, int *py1)
{
    int x0 = *px0, y0 = *py0, x1 = *px1, y1 = *py1;
    const int ox0 = x0, oy0 = y0, dx = x1 - x0, dy = y1 - y0;   /* always cut the original line */
    unsigned char c0 = outcode(x0, y0), c1 = outcode(x1, y1), c;
    int x = 0, y = 0;

    while (1) {
        if (!(c0 | c1)) break;              /* both inside */
        if (c0 & c1) return 0;              /* both beyond the same edge */
        c = c0 ? c0 : c1;
        if (c & 8)      { y = H - 1; x = ox0 + (int)((long)dx * (y - oy0) / dy); }
        else if (c & 4) { y = 0;     x = ox0 + (int)((long)dx * (y - oy0) / dy); }
        else if (c & 2) { x = W - 1; y = oy0 + (int)((long)dy * (x - ox0) / dx); }
        else            { x = 0;     y = oy0 + (int)((long)dy * (x - ox0) / dx); }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0); }
        else         { x1 = x; y1 = y; c1 = outcode(x1, y1); }
    }
    *px0 = x0; *py0 = y0; *px1 = x1; *py1 = y1;
    return 1;
}
