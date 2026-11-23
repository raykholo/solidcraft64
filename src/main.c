/* main.c - SolidCraft 64. STUB: lane C replaces this with the splash and the UI. */
#include <conio.h>
#include "model.h"

int main(void)
{
    feature f;

    model_new();
    f.type = F_RECT; f.ref = 0xFF; f.a = 0; f.b = 0; f.c = 800; f.d = 500;
    model_add(&f);
    f.type = F_EXTRUDE; f.ref = 0; f.a = 60; f.b = 0; f.c = 0; f.d = 0;
    model_add(&f);

    view_init();
    cputs("SOLIDCRAFT 64\r\n");
    view_draw();
    cgetc();
    view_done();
    return 0;
}
