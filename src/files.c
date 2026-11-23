/* files.c - the floppy: .SC64 parts, ASCII STL, and the directory. Drive 8.
 *
 * PETSCII. cc65 translates the characters of a C string literal and of a
 * character constant: 'A' becomes $C1 (a shifted letter), 'a' becomes $41,
 * and '\n' becomes $0D. Everything the 1541 and the part file want is
 * "ASCII" upper case ($41-$5A), so the source below writes its keywords in
 * LOWER case on purpose: "rect" is $52 $45 $43 $54 once compiled, and "\n"
 * is the carriage return the format ends its lines with. Never compare a
 * letter with 'A' or 'Z' here: use the numbers.
 *
 * The STL is the other way round: it wants real ASCII lower case ($61-$7A)
 * and a line feed, so stl_puts() maps $C1-$DA down by $60. STL words are
 * written in UPPER case in the source, and "\r" (which cc65 turns into
 * $0A) is the line feed.
 *
 * Names. The disk name is the part name, kept to A-Z, 0-9, space and '-',
 * and cut to 11 characters so that NAME + ".SC64" fits the 16 a Commodore
 * file name has (an STL gets 12 + ".STL"). Names from file_dir() come back
 * without the ".SC64", and file_load() and file_save() take a name with or
 * without it.
 *
 * Errors. 0 is success. 1-9 are KERNAL errors (5 is "device not present":
 * no drive). 20-74 are the drive's own numbers (62 file not found, 72 disk
 * full, 26 write protected). Ours:
 *   90 not a SolidCraft 64 part   (the first line is not "SC64 1")
 *   91 a line it can't read
 *   92 cut short                  (no END line; or an STL of a part with nothing to extrude)
 *   93 too long                   (more than 16 features, or too many bytes)
 */
#include <cbm.h>
#include <string.h>
#include "model.h"

#define DEV   8
#define LFN   1
#define CMD   15
#define ERR_NOT_PART 90
#define ERR_BAD_LINE 91
#define ERR_SHORT    92
#define ERR_LONG     93

static char buf[640];      /* a whole part file (16 lines of at most 34 bytes and a header), or STL output */
static part tmp;           /* a part being read: g_part is only touched when it all parses */
static char fname[24];     /* "stem.SC64,s,w" and friends */
static char cmdbuf[24];

/* ---- names ---- */

/* Copy name to out as a disk-name stem: A-Z 0-9 space '-', cut at the
 * first '.', at most max characters. Never empty. Returns its length. */
static unsigned char stem(char *out, const char *name, unsigned char max)
{
    unsigned char n = 0, c;

    while ((c = (unsigned char)*name++) != 0 && c != '.' && n < max) {
        if (c >= 0xC1 && c <= 0xDA) c -= 0x80;           /* a shifted letter, from the keyboard */
        if ((c >= 0x41 && c <= 0x5A) || (c >= '0' && c <= '9') || c == '-' || c == ' ')
            out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ') --n;
    if (!n) {
        strcpy(out, "part");        /* raw PART */
        n = 4;
    }
    out[n] = 0;
    return n;
}

/* ---- the drive ---- */

/* The drive's status, from the command channel. Returns its error number:
 * 0 for "00, OK". */
static unsigned char drive_status(void)
{
    char s[2];

    if (cbm_read(CMD, s, 2) < 2) return 5;
    return (unsigned char)((s[0] - '0') * 10 + (s[1] - '0'));
}

static void drain_status(void)
{
    char c;

    while (cbm_read(CMD, &c, 1) == 1 && c != 0x0D) ;
}

/* Open "stem.ext" for writing as a SEQ file on LFN, with the command
 * channel (CMD) left open for drive_status(). Scratches the old copy first.
 * We do not save with replace ("@0:name"): the 1541's save-with-replace has
 * a long-standing bug that can leave the replaced file with a broken block
 * chain and a wrong BAM. Scratch-then-write is safe on every drive, and the
 * time the old copy is gone is only the write of the few hundred bytes we
 * built in RAM first. The price: if the write then fails (a pulled cable;
 * not a full disk, as the scratch just made room), the old copy is lost
 * and the caller gets the error to say so. */
static unsigned char open_write(const char *name, unsigned char max, const char *ext)
{
    unsigned char e, n;

    n = stem(fname, name, max);
    fname[n++] = '.';
    strcpy(fname + n, ext);
    cmdbuf[0] = 's'; cmdbuf[1] = ':';                    /* raw "S:" */
    strcpy(cmdbuf + 2, fname);
    strcat(fname, ",s,w");

    if ((e = cbm_open(CMD, DEV, CMD, "")) != 0) return e;
    cbm_write(CMD, cmdbuf, (unsigned)strlen(cmdbuf));
    drain_status();                                      /* "01, FILES SCRATCHED", or nothing there */
    if ((e = cbm_open(LFN, DEV, 2, fname)) != 0) cbm_close(CMD);
    return e;
}

static unsigned char close_write(unsigned char e)
{
    cbm_close(LFN);
    if (!e) e = drive_status();                          /* 72 disk full, 26 write protected ... */
    cbm_close(CMD);
    return e;
}

/* ---- numbers ---- */

static char *bp;           /* the write cursor in buf */

static void bput(char c) { *bp++ = c; }

static void bstr(const char *s) { while (*s) *bp++ = *s++; }

static void bnum(int v)
{
    char t[6];
    unsigned char n = 0;
    unsigned u;

    if (v < 0) { bput('-'); u = (unsigned)(-v); } else u = (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) bput(t[--n]);
}

static void bnums(const feature *f, unsigned char first, const char *word, unsigned char n)
{
    int v[4];
    unsigned char k;

    v[0] = f->a; v[1] = f->b; v[2] = f->c; v[3] = f->d;
    bstr(word);
    if (first) { bnum(f->ref); bput(' '); }
    for (k = 0; k < n; ++k) { if (k) bput(' '); bnum(v[k]); }
}

/* ---- save ---- */

unsigned char file_save(const char *name)
{
    unsigned char i, e;
    const feature *f;

    /* The whole file is built in RAM first: see open_write(). */
    bp = buf;
    bstr("sc64 1\nname ");
    for (i = 0; i < NAME_LEN && g_part.name[i]; ++i) {
        unsigned char c = g_part.name[i];
        if (c >= 0xC1 && c <= 0xDA) c -= 0x80;      /* a shifted letter: write it as $41-$5A */
        bput(c);
    }
    bput('\n');
    for (i = 0; i < g_part.count; ++i) {
        f = &g_part.f[i];
        switch (f->type) {
        case F_RECT:    bnums(f, 0, "rect ", 4); break;
        case F_CIRCLE:  bnums(f, 0, "circle ", 3); break;
        case F_EXTRUDE: bnums(f, 1, "extrude ", 2); break;
        case F_CUT:     bnums(f, 1, "cut ", 2); break;
        default: continue;
        }
        bput('\n');
    }
    bstr("end\n");

    if ((e = open_write(name, 11, "sc64")) != 0) return e;
    if (cbm_write(LFN, buf, (unsigned)(bp - buf)) != (int)(bp - buf)) e = 5;
    e = close_write(e);
    if (!e) g_part.dirty = 0;
    return e;
}

/* ---- load ---- */

/* If *pp starts with keyword k (then a space or the end of the line), move
 * *pp past it and the space and return 1. */
static unsigned char kw(char **pp, const char *k)
{
    unsigned char n = (unsigned char)strlen(k);
    char *p = *pp;

    if (strncmp(p, k, n) != 0) return 0;
    if (p[n] == 0) { *pp = p + n; return 1; }
    if (p[n] != ' ') return 0;
    *pp = p + n + 1;
    return 1;
}

/* A signed decimal number at *pp, then a space or the end of the line. */
static unsigned char num(char **pp, int *out)
{
    char *p = *pp;
    long v = 0;
    unsigned char neg = 0, digits = 0;

    if (*p == '-') { neg = 1; ++p; }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        if (v > 32768L) return 0;
        ++digits;
    }
    if (!digits || (*p != 0 && *p != ' ')) return 0;
    if (neg) v = -v;
    if (v < -32768L || v > 32767L) return 0;
    if (*p == ' ') ++p;
    *pp = p;
    *out = (int)v;
    return 1;
}

static unsigned char is_sketch(unsigned char i)
{
    return i < tmp.count && (tmp.f[i].type == F_RECT || tmp.f[i].type == F_CIRCLE);
}

/* One feature line. Returns 0 if it is a good one. */
static unsigned char read_feature(char *p)
{
    feature *f;
    int v[4];
    unsigned char t, k, want;

    if (kw(&p, "rect")) { t = F_RECT; want = 4; }
    else if (kw(&p, "circle")) { t = F_CIRCLE; want = 3; }
    else if (kw(&p, "extrude")) { t = F_EXTRUDE; want = 3; }
    else if (kw(&p, "cut")) { t = F_CUT; want = 3; }
    else return ERR_BAD_LINE;

    for (k = 0; k < want; ++k) if (!num(&p, &v[k])) return ERR_BAD_LINE;
    if (*p) return ERR_BAD_LINE;
    if (tmp.count >= MAX_FEATURES) return ERR_LONG;

    f = &tmp.f[tmp.count];
    memset(f, 0, sizeof *f);
    f->type = t;
    f->ref = 0xFF;
    if (t == F_RECT) {
        f->a = v[0]; f->b = v[1]; f->c = v[2]; f->d = v[3];
    } else if (t == F_CIRCLE) {
        f->a = v[0]; f->b = v[1]; f->c = v[2];
    } else {
        if (v[0] < 0 || v[0] >= 0xFF || !is_sketch((unsigned char)v[0])) return ERR_BAD_LINE;
        f->ref = (unsigned char)v[0];
        f->a = v[1]; f->b = v[2];
    }
    ++tmp.count;
    return 0;
}

unsigned char file_load(const char *name)
{
    unsigned char e, line = 0, n;
    int got = 0;
    char *p, *q, *end;

    n = stem(fname, name, 11);
    fname[n++] = '.';
    strcpy(fname + n, "sc64,s,r");
    if ((e = cbm_open(CMD, DEV, CMD, "")) != 0) return e;
    if ((e = cbm_open(LFN, DEV, 2, fname)) != 0) { cbm_close(CMD); return e; }
    e = drive_status();                       /* 62 if there is no such file */
    if (!e) {
        got = cbm_read(LFN, buf, sizeof buf - 1);
        if (got < 0) e = 5;
    }
    cbm_close(LFN);
    cbm_close(CMD);
    if (e) return e;
    if (got >= (int)sizeof buf - 1) return ERR_LONG;

    /* Split into lines on CR (a stray LF at the start of one is skipped) and read them. */
    memset(&tmp, 0, sizeof tmp);
    buf[got] = 0;
    p = buf;
    end = buf + got;
    while (p < end) {
        for (q = p; q < end && *q != 0x0D; ++q) ;
        *q = 0;
        if (*p == 0x0A) ++p;                  /* CRLF, from a PC tool */
        if (line == 0) {
            if (strcmp(p, "sc64 1") != 0) return ERR_NOT_PART;
        } else if (line == 1) {
            if (!kw(&p, "name")) return ERR_BAD_LINE;
            for (n = 0; n < NAME_LEN && p[n]; ++n) tmp.name[n] = p[n];
            tmp.name[n] = 0;
            if (p[n]) return ERR_BAD_LINE;    /* more than 16 characters */
        } else if (strcmp(p, "end") == 0) {
            memcpy(&g_part, &tmp, sizeof g_part);
            g_part.dirty = 0;
            return 0;
        } else {
            if ((e = read_feature(p)) != 0) return e;
        }
        ++line;
        p = q + 1;
    }
    return line ? ERR_SHORT : ERR_NOT_PART;
}

/* ---- STL ---- */

/* Every number in the STL is in hundredths of a millimetre, as a long. */
typedef struct { long x, y, z; } pt;

static unsigned stl_fill;          /* bytes waiting in buf */

static void stl_flush(unsigned char *e)
{
    if (stl_fill) {
        if (cbm_write(LFN, buf, stl_fill) != (int)stl_fill) *e = 5;
        stl_fill = 0;
    }
}

/* Words are written in upper case in the source: see the top of the file. */
static void stl_puts(const char *s)
{
    unsigned char c;

    while ((c = (unsigned char)*s++) != 0) {
        if (c >= 0xC1 && c <= 0xDA) c -= 0x60;
        buf[stl_fill++] = (char)c;
    }
}

/* v with `places` decimals (v is in units of 10^-places). */
static void stl_dec(long v, unsigned char places)
{
    char t[14];
    unsigned char n = 0, i;
    unsigned long u;

    if (v < 0) { buf[stl_fill++] = '-'; u = (unsigned long)(-v); } else u = (unsigned long)v;
    for (i = 0; i < places; ++i) { t[n++] = (char)('0' + u % 10); u /= 10; }
    t[n++] = '.';
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) buf[stl_fill++] = t[--n];
}

/* The normal is in thousandths. */
static void facet(long nx, long ny, long nz, const pt *a, const pt *b, const pt *c, unsigned char *e)
{
    const pt *v[3];
    unsigned char i;

    v[0] = a; v[1] = b; v[2] = c;
    if (stl_fill > sizeof buf - 260) stl_flush(e);
    stl_puts("FACET NORMAL "); stl_dec(nx, 3); stl_puts(" "); stl_dec(ny, 3); stl_puts(" "); stl_dec(nz, 3);
    stl_puts("\rOUTER LOOP\r");
    for (i = 0; i < 3; ++i) {
        stl_puts("VERTEX "); stl_dec(v[i]->x, 2); stl_puts(" "); stl_dec(v[i]->y, 2); stl_puts(" ");
        stl_dec(v[i]->z, 2); stl_puts("\r");
    }
    stl_puts("ENDLOOP\rENDFACET\r");
}

static void quad(long nx, long ny, long nz, const pt *a, const pt *b, const pt *c, const pt *d, unsigned char *e)
{
    facet(nx, ny, nz, a, b, c, e);
    facet(nx, ny, nz, a, c, d, e);
}

/* A box from (x,y,z0) to (x+w,y+h,z1), all in hundredths. */
static void box(long x, long y, long z0, long w, long h, long z1, unsigned char *e)
{
    pt p[8];
    unsigned char i;

    for (i = 0; i < 8; ++i) {
        p[i].x = x + ((i & 1) ? w : 0);
        p[i].y = y + ((i & 2) ? h : 0);
        p[i].z = (i & 4) ? z1 : z0;
    }
    /* 0-3 are the bottom, 4-7 the top; bit 0 is +x and bit 1 is +y. Each
     * face is counter-clockwise seen from outside. */
    quad(0, 0, -1000, &p[0], &p[2], &p[3], &p[1], e);
    quad(0, 0, 1000, &p[4], &p[5], &p[7], &p[6], e);
    quad(0, -1000, 0, &p[0], &p[1], &p[5], &p[4], e);
    quad(1000, 0, 0, &p[1], &p[3], &p[7], &p[5], e);
    quad(0, 1000, 0, &p[3], &p[2], &p[6], &p[7], e);
    quad(-1000, 0, 0, &p[2], &p[0], &p[4], &p[6], e);
}

#define SEGS 16
/* cos of k * 22.5 degrees, in thousandths; sin k is cos (k - 4). */
static const int cs[SEGS] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
/* cos of (k + 0.5) * 22.5 degrees: the direction side face k looks. */
static const int mc[SEGS] = {981, 831, 556, 195, -195, -556, -831, -981, -981, -831, -556, -195, 195, 556, 831, 981};

/* A 16-sided prism, diameter d, centre (cx,cy), all in hundredths. */
static void prism(long cx, long cy, long z0, long d, long z1, unsigned char *e)
{
    static pt lo[SEGS], hi[SEGS];
    pt c0, c1;
    unsigned char k, j;
    long r = d / 2;

    c0.x = c1.x = cx; c0.y = c1.y = cy; c0.z = z0; c1.z = z1;
    for (k = 0; k < SEGS; ++k) {
        lo[k].x = hi[k].x = cx + r * cs[k] / 1000;
        lo[k].y = hi[k].y = cy + r * cs[(k + 12) & 15] / 1000;
        lo[k].z = z0; hi[k].z = z1;
    }
    for (k = 0; k < SEGS; ++k) {
        j = (k + 1) & 15;
        quad(mc[k], mc[(k + 12) & 15], 0, &lo[k], &lo[j], &hi[j], &hi[k], e);
        facet(0, 0, 1000, &c1, &hi[k], &hi[j], e);
        facet(0, 0, -1000, &c0, &lo[j], &lo[k], e);
    }
}

/* One solid per feature: an EXTRUDE is a PART; a CUT is the tool, and is
 * written as a solid of its own named CUT, because subtracting it is the
 * slicer's job (the 2000 way: bodies stay separate in the STL). */
static void body(unsigned char i, unsigned char *e)
{
    const feature *f = &g_part.f[i];
    const feature *s = &g_part.f[f->ref];
    long z0, z1, t;

    if (f->type == F_EXTRUDE) {
        z0 = (long)f->b * 10; z1 = z0 + (long)f->a * 10;
        stl_puts("SOLID PART\r");
    } else {
        z1 = (long)f->b * 10; z0 = z1 - (long)f->a * 10;
        stl_puts("SOLID CUT\r");
    }
    if (z0 > z1) { t = z0; z0 = z1; z1 = t; }
    if (s->type == F_RECT)
        box((long)s->a * 10, (long)s->b * 10, z0, (long)s->c * 10, (long)s->d * 10, z1, e);
    else
        prism((long)s->a * 10, (long)s->b * 10, z0, (long)s->c * 10, z1, e);
    stl_puts(f->type == F_EXTRUDE ? "ENDSOLID PART\r" : "ENDSOLID CUT\r");
}

unsigned char file_stl(const char *name)
{
    unsigned char i, e, any = 0;

    for (i = 0; i < g_part.count; ++i)
        if (g_part.f[i].type == F_EXTRUDE) any = 1;
    if (!any) return ERR_SHORT;

    if ((e = open_write(name, 12, "stl")) != 0) return e;
    stl_fill = 0;
    for (i = 0; i < g_part.count && !e; ++i)
        if (g_part.f[i].type == F_EXTRUDE || g_part.f[i].type == F_CUT)
            body(i, &e);
    stl_flush(&e);
    return close_write(e);
}

/* ---- the directory ---- */

unsigned char file_dir(char names[][NAME_LEN + 1], unsigned char max)
{
    unsigned char found = 0, n, c, len;
    unsigned char b[4];
    char line[40];
    char *q, *r;

    if (cbm_open(LFN, DEV, 0, "$") != 0) return 0;
    if (cbm_read(LFN, b, 2) != 2) { cbm_close(LFN); return 0; }          /* the load address */
    while (found < max) {
        if (cbm_read(LFN, b, 4) != 4) break;           /* link, then the block count */
        if (b[0] == 0 && b[1] == 0) break;             /* no link: the end */
        len = 0;
        for (;;) {                                     /* the text, up to a zero */
            if (cbm_read(LFN, &c, 1) != 1) { cbm_close(LFN); return found; }
            if (c == 0) break;
            if (len < sizeof line - 1) line[len++] = (char)c;
        }
        line[len] = 0;
        /* spaces, then "NAME", spaces, then the type. */
        for (q = line; *q && *q != '"'; ++q) ;
        if (!*q) continue;                             /* "BLOCKS FREE." */
        ++q;
        for (r = q; *r && *r != '"'; ++r) ;
        if (!*r) continue;
        n = (unsigned char)(r - q);
        for (++r; *r == ' '; ++r) ;
        if (strncmp(r, "seq", 3) != 0) continue;
        if (n < 6 || strncmp(q + n - 5, ".sc64", 5) != 0) continue;
        n -= 5;
        if (n > NAME_LEN) n = NAME_LEN;
        memcpy(names[found], q, n);
        names[found][n] = 0;
        ++found;
    }
    cbm_close(LFN);
    return found;
}
