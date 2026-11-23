/* draw.c against a plain full redraw: what the "screen" shows after present_rows() must equal the reference every frame. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a2600.h"
#include "draw.h"
#define W 854
#define H 480
static uint8_t fb8[W * H], shown[W * H], ref[W * H];
static long presented;
void present_rows(int y, int h) { memcpy(shown + y * W, fb8 + y * W, (size_t)h * W); presented += h; }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); static uint8_t rom[65536]; int n = (int)fread(rom, 1, sizeof rom, f); fclose(f);
    if (a26_load(rom, n)) return 2;
    draw_init(fb8, W, W, H);
    int frames = atoi(argv[2]), bad = 0; long rows = 0;
    for (int fr = 0; fr < frames; fr++) {
        a26_input(fr % 90 < 30 ? A26_UP : fr % 90 < 60 ? A26_RIGHT | A26_FIRE : A26_DOWN, 0, 0, 0, 0, 1, 1);
        a26_frame();
        int pal = a26_lines > 290, first = pal ? 40 : 34, nl = pal ? 228 : 210;
        draw_frame(a26_fb, first, nl);
        memset(ref, 128, sizeof ref);
        int y0 = (H - 2 * nl) / 2;
        for (int y = 0; y < nl; y++) for (int r = 0; r < 2; r++) for (int x = 0; x < 160; x++)
            memset(ref + (y0 + 2 * y + r) * W + 28 + x * 5, a26_fb[(first + y) * 160 + x], 5);
        if (memcmp(ref, shown, sizeof ref)) { bad++; if (bad < 4) printf("frame %d differs\n", fr); }
        rows += 2 * nl;
    }
    printf("%d frames, %d differ, rows presented %ld of %ld (%.0f%%)\n", frames, bad, presented, rows, 100.0 * presented / rows);
    return bad != 0;
}
