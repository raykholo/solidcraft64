/* main.c - SolidCraft 64: the splash, the menus, the dialogs and the feature
 * list. Text mode, written straight into screen RAM ($0400) and colour RAM
 * ($D800) with screen codes. Keyboard first; the joystick in port 2 turns the
 * wireframe.
 */
#include <conio.h>
#include <cbm.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

#define SCR      ((unsigned char *)0x0400)
#define COLR     ((unsigned char *)0xD800)
#define VBORDER  (*(volatile unsigned char *)0xD020)
#define VBG      (*(volatile unsigned char *)0xD021)
#define CIA1PA   (*(volatile unsigned char *)0xDC00)
#define CURCOL   (*(volatile unsigned char *)0x0286)
#define BYTE(a)  (*(volatile unsigned char *)(a))

#define C_BLACK  0
#define C_WHITE  1
#define C_CYAN   3
#define C_BLUE   6
#define C_YELLOW 7
#define C_DGREY  11
#define C_GREY   12
#define C_LBLUE  14
#define C_LGREY  15

#define REV      0x80
#define K_STOP   3
#define K_RETURN 13
#define K_DEL    20
#define K_DOWN   17
#define K_RIGHT  29
#define K_UP     145
#define K_LEFT   157
#define K_F1     133
#define K_F7     136

#define MAXLIST  16

static unsigned char sel;           /* highlighted feature              */
static unsigned char turbo;         /* does nothing, as turbos did      */
static unsigned char quit;
static unsigned char goto_menu;     /* 1 + menu to jump to, or 0        */
static char stat[32];
static char names[MAXLIST][NAME_LEN + 1];
static char *nptr[MAXLIST];

static void draw_ui(void);

/* ---- screen ------------------------------------------------------------ */

/* cc65 turns the letters in string and character literals into PETSCII, so
 * "A" is $C1 here. The keyboard gives $41. sc() takes either to screen code 1,
 * and getkey() hands back the literal form, so 'Y' compares equal to the Y key. */
static unsigned char sc(char c)
{
    if (c >= 0xC1 && c <= 0xDA) return c - 0xC0;
    if (c >= 0x41 && c <= 0x5A) return c - 0x40;
    return c;
}

static void put(unsigned char x, unsigned char y, unsigned char code, unsigned char col)
{
    unsigned i = y * 40u + x;
    SCR[i] = code;
    COLR[i] = col;
}

static void text(unsigned char x, unsigned char y, const char *s, unsigned char col, unsigned char rev)
{
    for (; *s; ++s, ++x) put(x, y, sc(*s) | rev, col);
}

static void fill(unsigned char x, unsigned char y, unsigned char w, unsigned char h,
                 unsigned char code, unsigned char col)
{
    unsigned char i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) put(x + i, y + j, code, col);
}

static void box(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char col)
{
    unsigned char i;
    fill(x, y, w, h, 32, col);
    for (i = 1; i < w - 1; ++i) { put(x + i, y, 0x40, col); put(x + i, y + h - 1, 0x40, col); }
    for (i = 1; i < h - 1; ++i) { put(x, y + i, 0x5D, col); put(x + w - 1, y + i, 0x5D, col); }
    put(x, y, 0x70, col);
    put(x + w - 1, y, 0x6E, col);
    put(x, y + h - 1, 0x6D, col);
    put(x + w - 1, y + h - 1, 0x7D, col);
}

static void clear_screen(void)
{
    fill(0, 0, 40, 25, 32, C_LBLUE);
}

/* ---- numbers ----------------------------------------------------------- */

/* tenths of a millimetre -> "-12.5" */
static char *fmt10(char *p, int v)
{
    unsigned u;
    if (v < 0) { *p++ = '-'; u = -v; } else u = v;
    utoa(u / 10, p, 10);
    p += strlen(p);
    *p++ = '.';
    *p++ = '0' + u % 10;
    *p = 0;
    return p;
}

static void cat10(char *o, int v) { fmt10(o + strlen(o), v); }

static void catu(char *o, unsigned v) { utoa(v, o + strlen(o), 10); }

/* "12.5" -> 125. Returns 0 if it isn't a number or doesn't fit. */
static unsigned char parse10(const char *s, int *out)
{
    unsigned char neg = 0, digits = 0, fd = 0;
    long ip = 0;

    if (*s == '-') { neg = 1; ++s; }
    for (; *s >= '0' && *s <= '9'; ++s) {
        ip = ip * 10 + (*s - '0');
        ++digits;
        if (ip > 3276) return 0;
    }
    if (*s == '.') {
        ++s;
        if (*s >= '0' && *s <= '9') { fd = *s - '0'; ++digits; }
    }
    if (!digits) return 0;
    ip = ip * 10 + fd;
    if (ip > 32767L) return 0;
    *out = neg ? -(int)ip : (int)ip;
    return 1;
}

/* ---- clock and status line ---------------------------------------------- */

static unsigned long jiffies(void)
{
    unsigned long a, b;
    do {
        a = ((unsigned long)BYTE(0xA0) << 16) | ((unsigned)BYTE(0xA1) << 8) | BYTE(0xA2);
        b = ((unsigned long)BYTE(0xA0) << 16) | ((unsigned)BYTE(0xA1) << 8) | BYTE(0xA2);
    } while (a != b);
    return a;
}

static void draw_status(void)
{
    fill(0, 24, 40, 1, 0xA0, C_WHITE);
    text(0, 24, stat, C_WHITE, REV);
}

static void status(const char *s)
{
    strncpy(stat, s, sizeof stat - 1);
    stat[sizeof stat - 1] = 0;
    draw_status();
}

/* TI$, as the Commodore keeps it: since power on. */
static void clock_tick(void)
{
    static unsigned long last = 0xFFFFFFFFUL;
    unsigned long s = jiffies() / 60;
    char t[9];
    unsigned h, m, r;

    if (s == last) return;
    last = s;
    s %= 86400UL;
    h = (unsigned)(s / 3600);
    m = (unsigned)((s % 3600) / 60);
    r = (unsigned)(s % 60);
    t[0] = '0' + h / 10; t[1] = '0' + h % 10; t[2] = ':';
    t[3] = '0' + m / 10; t[4] = '0' + m % 10; t[5] = ':';
    t[6] = '0' + r / 10; t[7] = '0' + r % 10; t[8] = 0;
    text(32, 24, t, C_WHITE, REV);
}

static unsigned char getkey(void)
{
    for (;;) {
        clock_tick();
        if (kbhit()) {
            unsigned char k = cgetc();
            return (k >= 0x41 && k <= 0x5A) ? k + 0x80 : k;
        }
    }
}

/* ---- the feature list --------------------------------------------------- */

static unsigned char kind(unsigned char t)
{
    return (t == F_RECT || t == F_CIRCLE) ? F_RECT : t;
}

static void fname(unsigned char i, char *o)
{
    unsigned char j, ord = 0;
    unsigned char k = kind(g_part.f[i].type);

    for (j = 0; j <= i; ++j) if (kind(g_part.f[j].type) == k) ++ord;
    strcpy(o, k == F_RECT ? "SKETCH" : k == F_EXTRUDE ? "EXTRUDE" : "CUT");
    catu(o, ord);
}

/* "1 SKETCH1 RECT 80.0 X 50.0" */
static void fline(unsigned char i, char *o)
{
    const feature *f = &g_part.f[i];

    o[0] = 0;
    catu(o, i + 1);
    strcat(o, " ");
    fname(i, o + strlen(o));
    strcat(o, " ");
    switch (f->type) {
    case F_RECT:
        strcat(o, "RECT ");
        cat10(o, f->c); strcat(o, " X "); cat10(o, f->d);
        break;
    case F_CIRCLE:
        strcat(o, "CIRCLE D ");
        cat10(o, f->c);
        break;
    case F_EXTRUDE:
        strcat(o, "OF "); catu(o, f->ref + 1); strcat(o, " UP "); cat10(o, f->a);
        break;
    case F_CUT:
        strcat(o, "OF "); catu(o, f->ref + 1); strcat(o, " DOWN "); cat10(o, f->a);
        break;
    }
    o[38] = 0;
}

static void draw_list(void)
{
    unsigned char i;
    char line[48];

    box(0, 2, 40, 18, C_CYAN);
    if (!g_part.count)
        text(2, 3, "NO FEATURES YET. F3 TO SKETCH.", C_LBLUE, 0);
    for (i = 0; i < g_part.count; ++i) {
        fline(i, line);
        if (i == sel) {
            fill(1, 3 + i, 38, 1, 0xA0, C_WHITE);
            text(1, 3 + i, line, C_WHITE, REV);
        } else {
            text(1, 3 + i, line, C_LBLUE, 0);
        }
    }
}

static void draw_ui(void)
{
    char t[20];

    clear_screen();
    fill(0, 0, 40, 1, 0xA0, C_WHITE);
    text(1, 0, "F1 FILE", C_WHITE, REV);
    text(10, 0, "F3 SKETCH", C_WHITE, REV);
    text(21, 0, "F5 FEATURES", C_WHITE, REV);
    text(33, 0, "F7 VIEW", C_WHITE, REV);

    text(0, 1, "PART ", C_LBLUE, 0);
    text(5, 1, g_part.name, C_WHITE, 0);
    if (g_part.dirty) put(5 + strlen(g_part.name), 1, 0x2A, C_YELLOW);   /* the asterisk */
    t[0] = 0;
    catu(t, g_part.count); strcat(t, "/16 FEATURES");
    text(26, 1, t, C_LBLUE, 0);
    if (turbo) text(19, 1, "TURBO", C_YELLOW, 0);

    draw_list();
    text(0, 21, "CRSR UP/DOWN: SELECT  F1-F7: MENUS", C_GREY, 0);
    text(0, 22, "RUN/STOP: CANCEL", C_GREY, 0);
    draw_status();
}

/* ---- menus and dialogs -------------------------------------------------- */

static signed char menu(unsigned char x, const char *const *it, unsigned char n)
{
    unsigned char w = 0, i, s = 0, k, l;

    for (i = 0; i < n; ++i) { l = strlen(it[i]); if (l > w) w = l; }
    w += 4;
    if (x + w > 40) x = 40 - w;
    for (;;) {
        box(x, 1, w, n + 2, C_WHITE);
        for (i = 0; i < n; ++i) {
            if (i == s) fill(x + 1, 2 + i, w - 2, 1, 0xA0, C_WHITE);
            text(x + 2, 2 + i, it[i], C_WHITE, i == s ? REV : 0);
        }
        k = getkey();
        if (k == K_DOWN) s = (s + 1) % n;
        else if (k == K_UP) s = (s + n - 1) % n;
        else if (k == K_RETURN) return s;
        else if (k == K_STOP) { goto_menu = 0; return -1; }
        else if (k >= K_F1 && k <= K_F7) { goto_menu = k - K_F1 + 1; return -1; }
        else if (k == K_RIGHT || k == K_LEFT) { goto_menu = 0; return k == K_RIGHT ? -2 : -3; }
    }
}

/* Yes or no, in a box. */
static unsigned char confirm(const char *l1, const char *l2)
{
    unsigned char k;

    box(3, 9, 34, 6, C_YELLOW);
    text(5, 10, l1, C_WHITE, 0);
    text(5, 11, l2, C_WHITE, 0);
    text(5, 13, "ARE YOU SURE? (Y/N)", C_YELLOW, 0);
    for (;;) {
        k = getkey();
        if (k == 'Y') return 1;
        if (k == 'N' || k == K_STOP) return 0;
    }
}

/* n numbers, in mm to one decimal place, held in tenths. Bit i of posmask
 * set means field i must be more than zero. Returns 1 for OK. */
static unsigned char dialog(const char *title, const char *const *labels, int *vals,
                            unsigned char n, unsigned char posmask)
{
    unsigned char i, cur = 0, len, fresh, k, y = 7;
    char buf[12], t[12];
    int v;

    box(4, y, 32, n + 5, C_YELLOW);
    text(6, y, " ", C_YELLOW, 0);
    text(7, y, title, C_WHITE, 0);
    text(7 + strlen(title), y, " ", C_YELLOW, 0);
    text(6, y + n + 3, "RETURN: OK  RUN/STOP: CANCEL", C_GREY, 0);
    for (i = 0; i < n; ++i) {
        text(6, y + 2 + i, labels[i], C_LBLUE, 0);
        text(26, y + 2 + i, "MM", C_GREY, 0);
    }
    fmt10(buf, vals[0]);
    len = strlen(buf);
    fresh = 1;
    for (;;) {
        for (i = 0; i < n; ++i) {
            fill(17, y + 2 + i, 8, 1, i == cur ? 0xA0 : 32, C_WHITE);
            if (i == cur) { memcpy(t, buf, len); t[len] = 0; }
            else fmt10(t, vals[i]);
            text(17, y + 2 + i, t, C_WHITE, i == cur ? REV : 0);
        }
        k = getkey();
        if (k == K_STOP) return 0;
        if (k == K_RETURN || k == K_DOWN || k == K_UP) {
            buf[len] = 0;
            if (!parse10(buf, &v)) { status("NUMBER, PLEASE: 12.5"); continue; }
            if ((posmask >> cur & 1) && v <= 0) { status("MUST BE MORE THAN ZERO"); continue; }
            vals[cur] = v;
            status("");
            if (k == K_RETURN && cur == n - 1) return 1;
            if (k == K_UP) { if (cur) --cur; }
            else if (cur < n - 1) ++cur;
            fmt10(buf, vals[cur]);
            len = strlen(buf);
            fresh = 1;
        } else if (k == K_DEL) {
            if (fresh) { len = 0; fresh = 0; } else if (len) --len;
        } else if ((k >= '0' && k <= '9') || k == '.' || k == '-') {
            char *dot;
            if (fresh) { len = 0; fresh = 0; }
            buf[len] = 0;
            dot = strchr(buf, '.');
            if (len >= 8) continue;
            if (k == '-' && len) continue;
            if (k == '.' && dot) continue;
            if (dot && buf + len - dot > 1) continue;      /* one decimal place */
            buf[len++] = k;
        }
    }
}

/* A file name: letters, digits and a hyphen. */
static unsigned char ask_name(const char *title, char *name)
{
    unsigned char len = strlen(name), k, i;

    for (i = 0; i < len; ++i) name[i] &= 0x7F;     /* disk names: $41-$5A, as BASIC types them */
    box(7, 9, 26, 6, C_YELLOW);
    text(9, 9, title, C_WHITE, 0);
    text(9, 13, "RETURN: OK  RUN/STOP: CANCEL", C_GREY, 0);
    for (;;) {
        fill(9, 11, 14, 1, 0xA0, C_WHITE);
        text(9, 11, name, C_WHITE, REV);
        put(9 + len, 11, 0x64, C_WHITE);
        k = getkey();
        if (k == K_STOP) return 0;
        if (k == K_RETURN && len) return 1;
        if (k == K_DEL) { if (len) name[--len] = 0; }
        else if (len < 11 && ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') || k == '-')) {
            name[len++] = k & 0x7F;
            name[len] = 0;
        }
    }
}

/* A list to pick from. Returns the index or -1. */
static signed char pick(const char *const *it, unsigned char n)
{
    signed char r = menu(4, it, n);
    goto_menu = 0;
    return r < 0 ? -1 : r;
}

/* ---- the gag ------------------------------------------------------------ */

static void oom_gag(void)
{
    clear_screen();
    text(0, 0, "?OUT OF MEMORY  ERROR", C_LBLUE, 0);
    text(0, 2, "READY.", C_LBLUE, 0);
    put(0, 3, 0xA0, C_LBLUE);
    getkey();
    draw_ui();
    status("NOT AN ERROR. 16 FEATURES IS IT.");
}

/* ---- actions ------------------------------------------------------------ */

static unsigned char sketches(const char **it, unsigned char *idx)
{
    static char lines[MAXLIST][24];
    unsigned char i, n = 0;
    char t[20];

    for (i = 0; i < g_part.count; ++i) {
        if (kind(g_part.f[i].type) != F_RECT) continue;
        fname(i, t);
        lines[n][0] = 0;
        catu(lines[n], i + 1);
        strcat(lines[n], " ");
        strcat(lines[n], t);
        it[n] = lines[n];
        idx[n++] = i;
    }
    return n;
}

static unsigned char room(void)
{
    if (g_part.count >= MAX_FEATURES) { oom_gag(); return 0; }
    return 1;
}

static void add_feature(const feature *f)
{
    unsigned char i = model_add(f);
    if (i == 0xFF) { oom_gag(); return; }
    sel = i;
    draw_ui();
    status("ADDED.");
}

static void do_rect(void)
{
    static const char *const lab[] = { "X", "Y", "WIDTH", "HEIGHT" };
    int v[4];
    feature f;

    if (!room()) return;
    v[0] = 0; v[1] = 0; v[2] = 800; v[3] = 500;
    if (!dialog("RECTANGLE", lab, v, 4, 0x0C)) { draw_ui(); return; }
    f.type = F_RECT; f.ref = 0xFF; f.a = v[0]; f.b = v[1]; f.c = v[2]; f.d = v[3];
    add_feature(&f);
}

static void do_circle(void)
{
    static const char *const lab[] = { "CENTRE X", "CENTRE Y", "DIAMETER" };
    int v[3];
    feature f;

    if (!room()) return;
    v[0] = 0; v[1] = 0; v[2] = 300;
    if (!dialog("CIRCLE", lab, v, 3, 0x04)) { draw_ui(); return; }
    f.type = F_CIRCLE; f.ref = 0xFF; f.a = v[0]; f.b = v[1]; f.c = v[2]; f.d = 0;
    add_feature(&f);
}

/* Extrude (cut = 0) or cut (cut = 1). */
static void do_solid(unsigned char cut)
{
    static const char *const lab_e[] = { "DEPTH", "BASE Z" };
    static const char *const lab_c[] = { "DEPTH", "TOP Z" };
    const char *it[MAXLIST];
    unsigned char idx[MAXLIST], n, i;
    signed char r = 0;
    int v[2], top = 0;
    feature f;

    if (!room()) return;
    n = sketches(it, idx);
    if (!n) { status("DRAW A SKETCH FIRST."); return; }
    if (n > 1) {
        r = pick(it, n);
        if (r < 0) { draw_ui(); return; }
    }
    v[0] = cut ? 50 : 100;
    v[1] = 0;
    if (cut) {
        for (i = 0; i < g_part.count; ++i)
            if (g_part.f[i].type == F_EXTRUDE && g_part.f[i].a + g_part.f[i].b > top)
                top = g_part.f[i].a + g_part.f[i].b;
        v[1] = top;
    }
    draw_ui();
    if (!dialog(cut ? "CUT" : "EXTRUDE", cut ? lab_c : lab_e, v, 2, 0x01)) { draw_ui(); return; }
    f.type = cut ? F_CUT : F_EXTRUDE; f.ref = idx[r]; f.a = v[0]; f.b = v[1]; f.c = 0; f.d = 0;
    add_feature(&f);
}

static void do_delete(void)
{
    char t[32], n[16];
    unsigned char before = g_part.count;

    if (!g_part.count) { status("NOTHING TO DELETE."); return; }
    fname(sel, n);
    strcpy(t, "DELETE ");
    strcat(t, n);
    strcat(t, "?");
    if (!confirm(t, "THINGS THAT USE IT GO TOO.")) { draw_ui(); return; }
    model_delete(sel);
    if (sel >= g_part.count && sel) sel = g_part.count - 1;
    draw_ui();
    t[0] = 0;
    catu(t, before - g_part.count);
    strcat(t, " DELETED.");
    status(t);
}

static unsigned char lose_ok(void)
{
    return !g_part.dirty || confirm("UNSAVED CHANGES WILL BE LOST.", "");
}

static void err(unsigned char rc)
{
    char t[32];
    strcpy(t, "DISK ERROR ");
    catu(t, rc);
    draw_ui();
    status(t);
}

static void do_new(void)
{
    if (!lose_ok()) { draw_ui(); return; }
    model_new();
    g_part.dirty = 0;
    sel = 0;
    draw_ui();
    status("NEW PART.");
}

static void do_open(void)
{
    unsigned char n, i, rc;
    signed char r;

    if (!lose_ok()) { draw_ui(); return; }
    draw_ui();
    n = file_dir(names, MAXLIST);
    if (!n) { status("NO PARTS ON THE DISK."); return; }
    for (i = 0; i < n; ++i) nptr[i] = names[i];
    r = pick((const char *const *)nptr, n);
    if (r < 0) { draw_ui(); return; }
    rc = file_load(names[(unsigned char)r]);
    sel = 0;
    if (rc) { err(rc); return; }
    g_part.dirty = 0;
    if (!g_part.name[0]) strcpy(g_part.name, names[(unsigned char)r]);
    draw_ui();
    status("LOADED.");
}

static void do_save(void)
{
    char name[NAME_LEN + 1];
    unsigned char rc;

    strcpy(name, g_part.name);
    if (!ask_name("SAVE AS", name)) { draw_ui(); return; }
    strcpy(g_part.name, name);                 /* the part takes the name it's saved under */
    rc = file_save(name);
    if (rc) { err(rc); return; }
    g_part.dirty = 0;
    draw_ui();
    status("SAVED.");
}

static void do_stl(void)
{
    char name[NAME_LEN + 1];
    unsigned char rc;

    strcpy(name, g_part.name);
    if (!ask_name("EXPORT STL AS", name)) { draw_ui(); return; }
    status("EXPORTING. THIS TAKES A WHILE.");
    rc = file_stl(name);
    if (rc) { err(rc); return; }
    draw_ui();
    status("STL WRITTEN.");
}

static void do_view(void)
{
    unsigned char k, j;
    signed char dy, dp;

    view_init();
    view_draw();
    for (;;) {
        dy = dp = 0;
        if (kbhit()) {
            k = cgetc();
            if (k == K_STOP) break;
            if (k == K_LEFT) dy = -2;
            else if (k == K_RIGHT) dy = 2;
            else if (k == K_UP) dp = -2;
            else if (k == K_DOWN) dp = 2;
        }
        j = ~CIA1PA;                         /* joystick in port 2 */
        if (j & 0x01) dp = -2;
        if (j & 0x02) dp = 2;
        if (j & 0x04) dy = -2;
        if (j & 0x08) dy = 2;
        if (dy || dp) { view_rotate(dy, dp); view_draw(); }
    }
    view_done();
    VBORDER = C_LBLUE;
    VBG = C_BLUE;
    draw_ui();
}

/* ---- the splash --------------------------------------------------------- */

static void splash(void)
{
    unsigned char j, s;
    char t[40];

    clear_screen();
    /* a block with a hole: front, top and side faces */
    fill(11, 5, 14, 7, 0xA0, C_LGREY);
    for (s = 1; s <= 3; ++s) {
        fill(11 + s, 5 - s, 14, 1, 0xA0, C_WHITE);
        fill(24 + s, 5 - s, 1, 7, 0xA0, C_GREY);
    }
    fill(15, 7, 6, 3, 32, C_BLACK);
    for (j = 0; j < 3; ++j) put(21, 7 + j, 0xA0, C_DGREY);   /* the hole has a wall */

    text(13, 14, "SOLIDCRAFT 64", C_WHITE, 0);
    text(10, 15, "PROFESSIONAL EDITION", C_YELLOW, 0);
    text(13, 17, "(C) 1982-1984", C_LBLUE, 0);
    text(1, 18, "PARODY. NOT AFFILIATED WITH SOLIDCRAFT.", C_GREY, 0);
    text(3, 20, "38911 BASIC BYTES FREE (THE ROM'S)", C_LBLUE, 0);
    if (BYTE(0x033B) == 0x53) {
        strcpy(t, "LOADED IN ");
        catu(t, BYTE(0x033D) * 256u + BYTE(0x033C));
        strcat(t, " SECONDS");
        text(10, 22, t, C_LBLUE, 0);
    }
    text(12, 24, "PRESS ANY KEY", C_WHITE, 0);
    while (!kbhit()) ;
    cgetc();
}

/* ---- the menus ---------------------------------------------------------- */

static const char *const m_file[] = { "NEW", "OPEN", "SAVE", "EXPORT STL", "QUIT" };
static const char *const m_sketch[] = { "RECTANGLE", "CIRCLE" };
static const char *const m_feat[] = { "EXTRUDE", "CUT", "DELETE" };
static char turbo_item[] = "TURBO: OFF";
static const char *const m_view[] = { "WIREFRAME", turbo_item, "ABOUT" };

static const char *const *const m_items[] = { m_file, m_sketch, m_feat, m_view };
static const unsigned char m_count[] = { 5, 2, 3, 3 };
static const unsigned char m_x[] = { 0, 9, 20, 32 };

static void act(unsigned char m, unsigned char r)
{
    draw_ui();
    switch (m * 8 + r) {
    case 0:  do_new(); break;
    case 1:  do_open(); break;
    case 2:  do_save(); break;
    case 3:  do_stl(); break;
    case 4:  if (lose_ok()) quit = 1; else draw_ui(); break;
    case 8:  do_rect(); break;
    case 9:  do_circle(); break;
    case 16: do_solid(0); break;
    case 17: do_solid(1); break;
    case 18: do_delete(); break;
    case 24: do_view(); break;
    case 25:
        turbo = !turbo;
        strcpy(turbo_item + 7, turbo ? "ON " : "OFF");
        draw_ui();
        status(turbo ? "TURBO ON. NOTHING CHANGED." : "TURBO OFF. STILL NOTHING.");
        break;
    case 26: splash(); draw_ui(); break;
    }
}

static void run_menu(unsigned char m)
{
    signed char r;

    for (;;) {
        draw_ui();
        r = menu(m_x[m], m_items[m], m_count[m]);
        if (r >= 0) { act(m, r); return; }
        if (goto_menu) { m = goto_menu - 1; goto_menu = 0; continue; }
        if (r == -2) m = (m + 1) & 3;          /* cursor right */
        else if (r == -3) m = (m + 3) & 3;     /* cursor left  */
        else break;
    }
    draw_ui();
}

int main(void)
{
    unsigned char k;

    cbm_k_bsout(142);                      /* upper case and graphics: cc65 starts in lower case */
    VBORDER = C_LBLUE;
    VBG = C_BLUE;
    model_new();
    g_part.dirty = 0;
    splash();
    draw_ui();
    status("READY.");
    while (!quit) {
        k = getkey();
        if (k >= K_F1 && k <= K_F7) run_menu(k - K_F1);
        else if (k == K_DOWN && sel + 1 < g_part.count) { ++sel; draw_list(); }
        else if (k == K_UP && sel) { --sel; draw_list(); }
    }
    VBORDER = C_LBLUE;
    VBG = C_BLUE;
    CURCOL = C_LBLUE;
    cbm_k_bsout(147);
    return 0;
}
