/* filesdemo.c - a test harness for src/files.c, run in VICE (make filesdemo).
 *
 * Loads a sample, saves it under a new name, exports the STL, reads it back,
 * and tries the ways a load can fail, printing every result on the screen.
 * tests/check_filesdemo.py then reads the disk image back on the host.
 */
#include <stdio.h>
#include <string.h>
#include "../src/model.h"

static char names[8][NAME_LEN + 1];

static void say(const char *what, unsigned char e)
{
    printf("%-22s %3u\n", what, e);
}

static void part_line(void)
{
    printf("  part %s, %u features%s\n", g_part.name, g_part.count, g_part.dirty ? ", DIRTY" : "");
}

int main(void)
{
    unsigned char n, i, e;

    printf("\nfiles demo\n");
    n = file_dir(names, 8);
    say("dir: names", n);
    for (i = 0; i < n; ++i) printf("  [%s]\n", names[i]);

    model_new();
    e = file_load("mounting pl"); say("load mounting pl", e); part_line();
    strcpy(g_part.name, "new plate");
    g_part.dirty = 1;
    e = file_save("new plate"); say("save new plate", e); part_line();
    e = file_save("new plate"); say("save again (replace)", e);
    e = file_stl("new plate"); say("stl new plate", e);

    model_new();
    e = file_load("new plate"); say("load new plate back", e); part_line();

    /* a bad load must leave the part alone */
    e = file_load("nope"); say("load nope (62)", e); part_line();
    e = file_load("bad1"); say("load bad1 (90)", e); part_line();
    e = file_load("bad2"); say("load bad2 (91)", e); part_line();
    e = file_load("bad3"); say("load bad3 (92)", e); part_line();
    e = file_load("bad4"); say("load bad4 (93)", e); part_line();

    e = file_load("flange"); say("load flange", e); part_line();
    e = file_stl("flange"); say("stl flange", e);
    e = file_save("flange copy"); say("save flange copy", e);
    n = file_dir(names, 8);
    say("dir: names", n);
    for (i = 0; i < n; ++i) printf("  [%s]\n", names[i]);
    printf("done\n");
    for (;;) ;
}
