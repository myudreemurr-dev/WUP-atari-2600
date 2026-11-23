#include <string.h>
#include "draw.h"
#include "a2600.h"

#define SCALE_X 5
#define X0 28                                            /* (854 - 800) / 2, rounded to a multiple of 4: word writes */
static uint8_t *fb8; static uint32_t stride; static int W, H;
static uint8_t prev[228 * A26_W] __attribute__((aligned(4)));
static int prev_ok, cur_first = -1, cur_n = -1;

void draw_init(uint8_t *f, uint32_t s, int w, int h) { fb8 = f; stride = s; W = w; H = h; prev_ok = 0; cur_first = cur_n = -1; }
void draw_reset(void) { prev_ok = 0; }

/* pixel groups g0..g1 (4 pixels = 20 bytes each) of a line */
static void expand_groups(uint8_t *a, const uint8_t *src, int g0, int g1) {
    if ((((uintptr_t)a) & 3) == 0) {
        uint32_t *d = (uint32_t *)(a + g0 * 20);
        for (int g = g0; g <= g1; g++) {
            const uint8_t *s4 = src + g * 4;
            uint32_t p0 = s4[0], p1 = s4[1], p2 = s4[2], p3 = s4[3];
            d[0] = p0 * 0x01010101u;
            d[1] = p0 | (p1 << 8) | (p1 << 16) | (p1 << 24);
            d[2] = p1 | (p1 << 8) | (p2 << 16) | (p2 << 24);
            d[3] = p2 | (p2 << 8) | (p2 << 16) | (p3 << 24);
            d[4] = p3 * 0x01010101u;
            d += 5;
        }
    } else {
        for (int x = g0 * 4; x < g1 * 4 + 4; x++) memset(a + x * SCALE_X, src[x], SCALE_X);
    }
}

int draw_frame(const uint8_t *fb, int first, int n) {
    int y0 = (H - 2 * n) / 2;
    if (n > 228) n = 228;
    if (first != cur_first || n != cur_n) {              /* a new layout (NTSC <-> PAL): black bars, repaint */
        cur_first = first; cur_n = n; prev_ok = 0;
        for (int y = 0; y < H; y++) memset(fb8 + (uint32_t)y * stride, 128, (size_t)W);
        present_rows(0, H);
    }
    const uint8_t *src = fb + first * A26_W;
    int top = -1, bottom = -1;
    for (int y = 0; y < n; y++, src += A26_W) {
        uint8_t *pv = prev + y * A26_W;
        int g0 = 0, g1 = A26_W / 4 - 1;
        if (prev_ok) {                                        /* only the groups of 4 pixels that changed */
            const uint32_t *a32 = (const uint32_t *)pv, *b32 = (const uint32_t *)src;
            while (g0 <= g1 && a32[g0] == b32[g0]) g0++;
            if (g0 > g1) continue;                            /* unchanged line: leave it on the screen */
            while (a32[g1] == b32[g1]) g1--;
        }
        memcpy(pv + g0 * 4, src + g0 * 4, (size_t)(g1 - g0 + 1) * 4);
        uint8_t *a = fb8 + (uint32_t)(y0 + 2 * y) * stride + X0;
        expand_groups(a, src, g0, g1);
        memcpy(a + stride + g0 * 20, a + g0 * 20, (size_t)(g1 - g0 + 1) * 20);
        if (top < 0) top = y;
        bottom = y;
    }
    prev_ok = 1;
    if (top < 0) return 0;
    present_rows(y0 + 2 * top, 2 * (bottom - top + 1));
    return 2 * (bottom - top + 1);
}
