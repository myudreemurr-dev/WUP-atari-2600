/* PC harness: run a ROM headless, scripted input, write frames as PPM (320x? scaled 2x1). usage: run rom frames out_prefix [dump_frames...] [-j frame:bits ...] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a2600.h"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("rom"); return 2; }
    static uint8_t rom[65536]; int n = (int)fread(rom, 1, sizeof rom, f); fclose(f);
    int r = a26_load(rom, n); printf("load %d bytes -> %d (%s)\n", n, r, r ? "-" : a26_mapper_name()); if (r) return 1;
    int frames = atoi(argv[2]); const char *pre = argv[3];
    int dump[64], nd = 0; struct { int fr; unsigned bits; int rst, sel; } js[64]; int nj = 0;
    for (int i = 4; i < argc; i++) {
        if (argv[i][0] == 'j') { int fr; unsigned b; int rs = 0, se = 0; sscanf(argv[i] + 1, "%d:%u:%d:%d", &fr, &b, &rs, &se); js[nj].fr = fr; js[nj].bits = b; js[nj].rst = rs; js[nj].sel = se; nj++; }
        else dump[nd++] = atoi(argv[i]);
    }
    unsigned long long h = 14695981039346656037ULL; (void)h;
    unsigned bits = 0; int rst = 0, sel = 0; long samples = 0; int16_t au[2000]; long long energy = 0;
    for (int fr = 0; fr < frames; fr++) {
        for (int j = 0; j < nj; j++) if (js[j].fr == fr) { bits = js[j].bits; rst = js[j].rst; sel = js[j].sel; }
        a26_input(bits, 0, rst, sel, 0, 1, 1);
        a26_frame();
        for (int i = 0; i < 262 * 160; i++) { h ^= a26_fb[i]; h *= 1099511628211ULL; }
        int m = a26_audio(au, 2000); samples += m; for (int i = 0; i < m; i++) energy += (long long)au[i] * au[i] / 1000;
        for (int d = 0; d < nd; d++) if (dump[d] == fr) {
            char name[256]; snprintf(name, sizeof name, "%s_%03d.ppm", pre, fr);
            FILE *o = fopen(name, "wb"); int h = 262; fprintf(o, "P6\n320 %d\n255\n", h);
            for (int y = 0; y < h; y++) for (int x = 0; x < 160; x++) {
                uint32_t c = a26_palette(a26_fb[y * 160 + x]); unsigned char px[3] = { c >> 16, c >> 8, c };
                fwrite(px, 3, 1, o); fwrite(px, 3, 1, o);
            }
            fclose(o);
        }
        if (fr % 60 == 0 || fr == frames - 1) { printf("frame %d: lines=%d cyc=%u\n", fr, a26_lines, a26_cycles()); }
    }
    printf("fbhash %016llx\n", h);
    printf("audio samples %ld, energy %lld\n", samples, energy);
    return 0;
}
