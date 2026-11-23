/* model.c - the part in memory. */
#include <string.h>
#include "model.h"

part g_part;

void model_new(void)
{
    memset(&g_part, 0, sizeof g_part);
    /* "part1" in lower case: cc65 makes that $50 $41 $52 $54 $31, the
     * upper case the disk and the part file use (see files.c). */
    strcpy(g_part.name, "part1");
}

unsigned char model_add(const feature *f)
{
    if (g_part.count >= MAX_FEATURES) return 0xFF;
    g_part.f[g_part.count] = *f;
    g_part.dirty = 1;
    return g_part.count++;
}

/* Delete feature i, and every later feature that uses it (or uses one of
 * those). References after the gap are renumbered. */
void model_delete(unsigned char i)
{
    unsigned char j, k, n;
    unsigned char gone[MAX_FEATURES];

    if (i >= g_part.count) return;
    memset(gone, 0, sizeof gone);
    gone[i] = 1;
    for (j = i + 1; j < g_part.count; ++j)
        if (g_part.f[j].ref != 0xFF && gone[g_part.f[j].ref]) gone[j] = 1;

    n = 0;
    for (j = 0; j < g_part.count; ++j) {
        if (gone[j]) continue;
        g_part.f[n] = g_part.f[j];
        if (g_part.f[n].ref != 0xFF) {
            /* refs only ever point at earlier, surviving sketches: count the gaps */
            unsigned char m, shift = 0;
            k = g_part.f[n].ref;
            for (m = 0; m < k; ++m) if (gone[m]) ++shift;
            g_part.f[n].ref = k - shift;
        }
        ++n;
    }
    g_part.count = n;
    g_part.dirty = 1;
}
