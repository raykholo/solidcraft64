/* view.c - STUB. Lane A replaces this with the hi-res wireframe. */
#include <conio.h>
#include "model.h"

void view_init(void) { clrscr(); }
void view_draw(void) { cprintf("%u FEATURES\r\n", g_part.count); }
void view_rotate(signed char dyaw, signed char dpitch) { (void)dyaw; (void)dpitch; }
void view_text(unsigned char col, unsigned char row, const char *s) { gotoxy(col, row); cputs(s); }
void view_done(void) { }
