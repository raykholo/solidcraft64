/* Host test for src/clip.c: gcc -o build/clip_test tests/clip_test.c && build/clip_test
 * A clipped line must lie on the screen, and must stay within one pixel of the original line. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/clip.c"

int main(void)
{
    int n, bad = 0, seen = 0;
    srand(1);
    for (n = 0; n < 200000; ++n) {
        int x0 = rand() % 1200 - 400, y0 = rand() % 900 - 350, x1 = rand() % 1200 - 400, y1 = rand() % 900 - 350;
        int a = x0, b = y0, c = x1, d = y1, t;
        double len = hypot(x1 - x0, y1 - y0), dist;
        unsigned char r = clip_line(&a, &b, &c, &d);
        /* does the real segment touch the screen? sample it */
        int touches = 0;
        for (t = 0; t <= 2000 && !touches; ++t) {
            double fx = x0 + (x1 - x0) * t / 2000.0, fy = y0 + (y1 - y0) * t / 2000.0;
            if (fx >= 0.5 && fx < 318.5 && fy >= 0.5 && fy < 198.5) touches = 1;
        }
        if (r) {
            ++seen;
            if (a < 0 || a > 319 || c < 0 || c > 319 || b < 0 || b > 199 || d < 0 || d > 199) { ++bad; continue; }
            if (len > 0) {
                dist = fabs((x1 - x0) * (double)(y0 - b) - (y1 - y0) * (double)(x0 - a)) / len;
                if (dist > 1.5) { ++bad; printf("far %d,%d-%d,%d -> %d,%d-%d,%d\n",x0,y0,x1,y1,a,b,c,d);}
                dist = fabs((x1 - x0) * (double)(y0 - d) - (y1 - y0) * (double)(x0 - c)) / len;
                if (dist > 1.5) { ++bad; printf("far %d,%d-%d,%d -> %d,%d-%d,%d\n",x0,y0,x1,y1,a,b,c,d);}
            }
        } else if (touches) { ++bad; if (bad < 20) printf("rej %d,%d-%d,%d\n", x0,y0,x1,y1); }
    }
    printf("%d lines, %d visible, %d bad\n", n, seen, bad);
    return bad != 0;
}
