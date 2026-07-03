/* host_demo.c -- Linux harness: render WhittyDemo frames to PPM for visual
 * validation before the Amiga build (family shared-core method).
 * Usage: host_demo <outdir> <tick> [tick...] */
#include <stdio.h>
#include <stdlib.h>
#include "demo_core.h"

#define W 864
#define H 486

int main(int argc, char **argv)
{
    static unsigned char fb[W * H];
    static unsigned char rgb[W * H * 3];
    int a;
    const char *sel_env;

    if (argc < 3) {
        fprintf(stderr, "usage: %s outdir tick [tick...]\n", argv[0]);
        return 1;
    }
    demo_init(W, H);
    demo_set_text_variant(2);
    sel_env = getenv("SELECTOR_SELECTION");
    demo_set_selector_selection(sel_env ? atoi(sel_env) : 0);
    for (a = 2; a < argc; a++) {
        int tick = atoi(argv[a]);
        char path[512];
        FILE *f;
        long i;
        demo_frame(fb, tick);
        for (i = 0; i < (long)W * H; i++) {
            rgb[i * 3 + 0] = demo_pal[fb[i]][0];
            rgb[i * 3 + 1] = demo_pal[fb[i]][1];
            rgb[i * 3 + 2] = demo_pal[fb[i]][2];
        }
        snprintf(path, sizeof(path), "%s/frame_%05d.ppm", argv[1], tick);
        f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        fwrite(rgb, 1, sizeof(rgb), f);
        fclose(f);
        printf("wrote %s\n", path);
    }
    return 0;
}
