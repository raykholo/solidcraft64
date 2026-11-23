/* line.h - the assembly half of the wireframe (line.s). */
#ifndef SC64_LINE_H
#define SC64_LINE_H

extern int line_x0, line_x1;
extern unsigned char line_y0, line_y1;
extern unsigned char rowlo[200], rowhi[200];   /* bitmap address of each pixel row */
extern unsigned char fontbuf[512];             /* glyphs for screen codes 0..63    */

void line_draw(void);    /* (x0,y0)-(x1,y1), already inside 0..319 x 0..199, OR-plot */
void line_clear(void);   /* the whole bitmap to background                           */
void font_load(void);    /* character ROM -> fontbuf                                 */

/* fast fixed-point multiplies (line.s), round towards zero */
extern int mq_a, mq_b;
extern unsigned int mk_k;
extern unsigned char mk_sh;
int mulq(void);     /* (mq_a * mq_b) >> 7: mq_b is a sine-table value, -127..127 */
int mulk(void);     /* (mq_a * mk_k) >> mk_sh: mk_k unsigned 16 bits             */

/* clip.c: Cohen-Sutherland to 320 x 200. Returns 0 when nothing is visible. */
unsigned char clip_line(int *x0, int *y0, int *x1, int *y1);

#endif
