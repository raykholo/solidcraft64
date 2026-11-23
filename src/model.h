/* model.h - the part, as SolidCraft 64 keeps it in memory.
 *
 * Shared by every module. Change it only with docs/FORMAT.md in the same commit.
 *
 * Lengths are in tenths of a millimetre, as signed 16-bit ints: 1234 is
 * 123.4 mm, and the biggest part is 3276.7 mm across, which is bigger than
 * any desk a Commodore 64 was ever put on.
 */
#ifndef SC64_MODEL_H
#define SC64_MODEL_H

#define MAX_FEATURES 16
#define NAME_LEN     16

/* Feature types. The numbers are written to disk: never renumber them. */
#define F_NONE    0
#define F_RECT    1   /* sketch: a = x, b = y (lower-left corner), c = width, d = height  */
#define F_CIRCLE  2   /* sketch: a = cx, b = cy, c = diameter, d unused                    */
#define F_EXTRUDE 3   /* ref = sketch's feature index, a = depth (up from z = b), b = base z */
#define F_CUT     4   /* ref = sketch's feature index, a = depth (down from z = b), b = top z */

typedef struct {
    unsigned char type;        /* F_*                                   */
    unsigned char ref;         /* index of the sketch it uses, or 0xFF  */
    int a, b, c, d;            /* see the F_* comments                  */
} feature;

typedef struct {
    char name[NAME_LEN + 1];   /* upper case, PETSCII-safe ASCII        */
    unsigned char count;       /* features in use, 0..MAX_FEATURES      */
    unsigned char dirty;       /* changed since the last save           */
    feature f[MAX_FEATURES];
} part;

extern part g_part;

/* model.c */
void model_new(void);                         /* empty part named PART1          */
unsigned char model_add(const feature *f);    /* index, or 0xFF when it's full   */
void model_delete(unsigned char i);           /* and anything that refers to it  */

/* view.c (lane A): draw the part as a wireframe on the hi-res screen. */
void view_init(void);                         /* hi-res on, screen cleared       */
void view_draw(void);                         /* the whole part, current angles  */
void view_rotate(signed char dyaw, signed char dpitch);  /* in 1/64ths of a turn */
void view_text(unsigned char col, unsigned char row, const char *s); /* 8x8 text */
void view_done(void);                         /* back to the text screen         */

/* files.c (lane B). Return 0 on success, or a KERNAL/DOS error number;
 * file_dir returns how many names it found. */
unsigned char file_save(const char *name);    /* "NAME.SC64" on drive 8          */
unsigned char file_load(const char *name);
unsigned char file_stl(const char *name);     /* ASCII STL, "NAME.STL"           */
unsigned char file_dir(char names[][NAME_LEN + 1], unsigned char max);

#endif
