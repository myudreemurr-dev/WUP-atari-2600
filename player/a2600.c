/* Atari 2600 core (see a2600.h). The CPU counts every bus access as a cycle; the TIA is brought up to the cycle of an
 * access only when it is touched (a write to a TIA register takes effect at its own colour clock), and draws spans of
 * pixels at a time. Built with -DA26_FLAT_TEST it is just a 6502 on 64 KB of RAM (tests/cpu_test.c). */
#include <string.h>
#include "a2600.h"

uint8_t a26_fb[A26_MAXLINES * A26_W] __attribute__((aligned(4)));
int a26_lines = 262;

/* ------------------------------------------------------------------------------------------------ machine state */
static uint32_t cyc;                          /* CPU cycles since reset (wraps: only differences are used) */
static uint8_t ram[128];
static int frame_done;

/* ---- cartridge: four 1 KB windows into the ROM cover $1000..$1FFF whatever the bank switching */
static const uint8_t *rom; static int rsize;
enum { M_2K, M_4K, M_F8, M_F6, M_F4, M_FA, M_E0, M_3F };
static int mapper, sc, fa;
static uint8_t xram[256];
static const uint8_t *seg[4];
static int nbanks;
static const char *const mnames[] = { "2K", "4K", "F8", "F6", "F4", "FA", "E0", "3F" };

static void set4(int b) { b %= nbanks; for (int i = 0; i < 4; i++) seg[i] = rom + b * 4096 + i * 1024; }
static void set_3f(int b) { b %= nbanks; seg[0] = rom + b * 2048; seg[1] = seg[0] + 1024; }
static void hot(unsigned a) {
    switch (mapper) {
    case M_F8: if (a >= 0xFF8 && a <= 0xFF9) set4((int)a - 0xFF8); break;
    case M_F6: if (a >= 0xFF6 && a <= 0xFF9) set4((int)a - 0xFF6); break;
    case M_F4: if (a >= 0xFF4 && a <= 0xFFB) set4((int)a - 0xFF4); break;
    case M_FA: if (a >= 0xFF8 && a <= 0xFFA) set4((int)a - 0xFF8); break;
    case M_E0: if (a >= 0xFE0 && a <= 0xFF7) seg[(a - 0xFE0) >> 3] = rom + (a & 7) * 1024; break;
    }
}
static uint8_t cart_rd(unsigned a) {
    if (mapper >= M_F8) hot(a);
    if (sc && a >= 0x80 && a < 0x100) return xram[a - 0x80];
    if (fa && a >= 0x100 && a < 0x200) return xram[a - 0x100];
    return seg[a >> 10][a & 0x3FF];
}
static void cart_wr(unsigned a, uint8_t v) {
    hot(a);
    if (sc && a < 0x80) xram[a] = v;
    else if (fa && a < 0x100) xram[a] = v;
}
static int has_sig(const uint8_t *p, int n, const uint8_t *s, int sl) {
    for (int i = 0; i + sl <= n; i++) if (!memcmp(p + i, s, sl)) return 1;
    return 0;
}
static int is_3f(void) {                      /* STA $3F ($85 $3F) at least twice */
    int c = 0; for (int i = 0; i + 1 < rsize; i++) if (rom[i] == 0x85 && rom[i + 1] == 0x3F) c++;
    return c >= 2;
}
static int is_e0(void) {
    static const uint8_t sigs[][3] = { {0x8D,0xE0,0x1F}, {0x8D,0xE0,0x5F}, {0x8D,0xE9,0x1F}, {0x0C,0xE0,0x1F}, {0xAD,0xE0,0x1F},
                                       {0xAD,0xE9,0xFF}, {0xAD,0xED,0xFF}, {0x0C,0xE0,0xFF}, {0xAD,0xE0,0xFF} };
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) if (has_sig(rom, rsize, sigs[i], 3)) return 1;
    return 0;
}
static int is_sc(int banks) {                 /* the first 256 bytes of every 4K bank are one repeated value: that is the RAM's window */
    for (int b = 0; b < banks; b++)
        for (int i = 0; i < 256; i++) if (rom[b * 4096 + i] != rom[0]) return 0;
    return 1;
}
int a26_load(const uint8_t *r, int size) {
    rom = r; rsize = size; sc = fa = 0;
    if (size == 2048) mapper = M_2K;
    else if (size == 4096) mapper = M_4K;
    else if (size == 8192) mapper = is_e0() ? M_E0 : is_3f() ? M_3F : M_F8;
    else if (size == 12288) mapper = M_FA;
    else if (size == 16384) mapper = is_3f() ? M_3F : M_F6;
    else if (size == 32768) mapper = is_3f() ? M_3F : M_F4;
    else return -1;
    if (mapper == M_F8 || mapper == M_F6 || mapper == M_F4) sc = is_sc(size / 4096);
    if (mapper == M_FA) fa = 1;
    nbanks = (mapper == M_3F) ? size / 2048 : size / 4096;
    a26_reset();
    return 0;
}
const char *a26_mapper_name(void) { return mnames[mapper]; }
static void cart_reset(void) {
    memset(xram, 0, sizeof xram);
    switch (mapper) {
    case M_2K: seg[0] = seg[2] = rom; seg[1] = seg[3] = rom + 1024; break;
    case M_4K: for (int i = 0; i < 4; i++) seg[i] = rom + i * 1024; break;
    case M_E0: for (int i = 0; i < 3; i++) seg[i] = rom + (i == 0 ? 4 : i == 1 ? 5 : 6) * 1024; seg[3] = rom + 7 * 1024; break;
    case M_3F: set_3f(0); seg[2] = rom + (nbanks - 1) * 2048; seg[3] = seg[2] + 1024; break;
    default: set4(nbanks - 1); break;
    }
}

/* ------------------------------------------------------------------------------------------------ RIOT */
static uint8_t swcha = 0xFF, swchb = 0x3F | 0x08 | 0xC0, inpt4 = 0x80, inpt5 = 0x80;
static uint32_t t_start; static int32_t t_rem0 = 0x3FF * 1024; static int t_int = 1024;
static uint8_t riot_read(unsigned a) {
    if (!(a & 4)) {
        switch (a & 3) { case 0: return swcha; case 2: return swchb; default: return 0; }
    }
    int32_t rem = t_rem0 - (int32_t)(cyc - t_start);    /* cycles to go, counted from the write cycle (INTIM shows V-1 right away) */
    if (a & 1) return rem < 0 ? 0x80 : 0;
    if (rem >= 0) return (uint8_t)(rem / t_int);        /* 0 for one interval, then it wraps to 255 and counts every cycle */
    return (uint8_t)rem;
}
static void riot_write(unsigned a, uint8_t v) {
    if ((a & 4) && (a & 0x10)) {
        static const int iv[4] = { 1, 8, 64, 1024 };
        t_int = iv[a & 3]; t_rem0 = (int32_t)v * t_int; t_start = cyc;
    }
}

/* ------------------------------------------------------------------------------------------------ TIA */
static struct {
    uint8_t vsync, vblank, nusiz[2], colup[2], colupf, colubk, ctrlpf, refp[2];
    uint8_t pf[3], grp[2], grpold[2], enam[2], enabl, enablold, hm[5], vdelp[2], vdelbl, resmp[2];
    int pos[5];                               /* P0 P1 M0 M1 BL: first pixel (0..159) */
    uint64_t cx;                              /* collision latches: byte n = CX register n (bits 7 and 6) */
    uint32_t pfmask[5];                       /* the playfield as 160 pixels */
} T;
static uint32_t tia_cyc;
static int tx, ty, hmove_blank;
static uint64_t cxtab[64];                    /* object-presence mask -> collision bits it sets */
enum { O_P0 = 1, O_P1 = 2, O_M0 = 4, O_M1 = 8, O_BL = 16, O_PF = 32 };
static void cxinit(void) {
    for (int m = 0; m < 64; m++) {
        uint64_t r = 0;
#define C(reg, bit, a, b) if ((m & (a)) && (m & (b))) r |= (uint64_t)(bit) << ((reg) * 8)
        C(0, 0x80, O_M0, O_P1); C(0, 0x40, O_M0, O_P0);
        C(1, 0x80, O_M1, O_P0); C(1, 0x40, O_M1, O_P1);
        C(2, 0x80, O_P0, O_PF); C(2, 0x40, O_P0, O_BL);
        C(3, 0x80, O_P1, O_PF); C(3, 0x40, O_P1, O_BL);
        C(4, 0x80, O_M0, O_PF); C(4, 0x40, O_M0, O_BL);
        C(5, 0x80, O_M1, O_PF); C(5, 0x40, O_M1, O_BL);
        C(6, 0x80, O_BL, O_PF);
        C(7, 0x80, O_P0, O_P1); C(7, 0x40, O_M0, O_M1);
#undef C
        cxtab[m] = r;
    }
}
static uint32_t omask[5][5];                 /* P0 P1 M0 M1 BL: the pixels each covers (see render) */
static uint8_t odirty = 0x1F;                /* masks to rebuild (bit per object) */
static uint8_t pfrow[A26_W]; static int pfrow_ok;

static void pf_rebuild(void) {
    pfrow_ok = 0;
    uint32_t half = (uint32_t)(T.pf[0] >> 4);
    uint32_t p1 = T.pf[1], r = 0;
    for (int i = 0; i < 8; i++) if (p1 & (0x80 >> i)) r |= 1u << i;       /* PF1: bit 7 first */
    half |= r << 4; half |= (uint32_t)T.pf[2] << 12;                      /* PF2: bit 0 first */
    uint32_t bits[160 / 4 > 40 ? 40 : 40];
    int mirror = T.ctrlpf & 1;
    for (int k = 0; k < 20; k++) {
        bits[k] = (half >> k) & 1;
        bits[20 + k] = mirror ? (half >> (19 - k)) & 1 : (half >> k) & 1;
    }
    memset(T.pfmask, 0, sizeof T.pfmask);
    for (int k = 0; k < 40; k++) if (bits[k]) for (int j = 0; j < 4; j++) { int px = k * 4 + j; T.pfmask[px >> 5] |= 1u << (px & 31); }
}

/* The picture is drawn from layers: a cached row of playfield / background colours (copied into the line), and for each
 * object (P0 P1 M0 M1 BL) a 160-bit mask of the pixels it covers (rebuilt only when one of its registers changed). Only the pixels
 * an object covers are looked at one by one; collisions are bit-wise ANDs of the masks. */
static uint8_t rev8[256]; static uint16_t dbl8[256];
static void tables_init(void) {
    for (int i = 0; i < 256; i++) {
        int r = 0, d = 0;
        for (int k = 0; k < 8; k++) if (i & (1 << k)) { r |= 0x80 >> k; d |= 3 << (2 * k); }
        rev8[i] = (uint8_t)r; dbl8[i] = (uint16_t)d;
    }
}
static const uint8_t copyoff[8][3] = { {0,0,0}, {0,16,0}, {0,32,0}, {0,16,32}, {0,64,0}, {0,0,0}, {0,32,64}, {0,0,0} };
static const uint8_t ncopies[8] = { 1, 2, 2, 3, 2, 1, 3, 1 };
static inline void place(uint32_t *m, int s, uint32_t pat) {      /* pat: up to 32 pixels, pixel 0 = bit 0, starting at pixel s (wraps at 160) */
    int w = s >> 5, b = s & 31;
    m[w] |= pat << b;
    if (b) { uint32_t hi = pat >> (32 - b); if (hi) m[w == 4 ? 0 : w + 1] |= hi; }
}
static void obj_rebuild(int i) {
    uint32_t *m = omask[i];
    memset(m, 0, 5 * sizeof(uint32_t));
    if (i < 2) {
        unsigned g = T.vdelp[i] ? T.grpold[i] : T.grp[i];
        if (!g) return;
        int t = T.nusiz[i] & 7;
        uint32_t pat = (T.refp[i] & 8) ? g : rev8[g];                 /* 8 pixels, pixel 0 first */
        if (t == 5) { pat = dbl8[pat]; }
        else if (t == 7) { uint32_t d = dbl8[pat]; pat = (uint32_t)dbl8[d & 0xFF] | ((uint32_t)dbl8[d >> 8] << 16); }
        for (int c = 0; c < ncopies[t]; c++) place(m, (T.pos[i] + copyoff[t][c]) % 160, pat);
    } else if (i < 4) {
        int k = i - 2, t = T.nusiz[k] & 7;
        if (!((T.enam[k] & 2) && !(T.resmp[k] & 2))) return;
        uint32_t pat = (1u << (1 << ((T.nusiz[k] >> 4) & 3))) - 1;     /* 1, 2, 4 or 8 pixels wide */
        for (int c = 0; c < ncopies[t]; c++) place(m, (T.pos[i] + copyoff[t][c]) % 160, pat);
    } else {
        if (!(((T.vdelbl ? T.enablold : T.enabl) & 2))) return;
        place(m, T.pos[4], (1u << (1 << ((T.ctrlpf >> 4) & 3))) - 1);
    }
}
static void pfrow_rebuild(void) {
    const uint8_t c0 = T.colup[0] >> 1, c1 = T.colup[1] >> 1, cpf = T.colupf >> 1, cbk = T.colubk >> 1;
    const int score = T.ctrlpf & 2;
    for (int x = 0; x < A26_W; x += 4) {
        uint8_t col = ((T.pfmask[x >> 5] >> (x & 31)) & 1) ? (score ? (x < 80 ? c0 : c1) : cpf) : cbk;
        pfrow[x] = pfrow[x + 1] = pfrow[x + 2] = pfrow[x + 3] = col;
    }
    pfrow_ok = 1;
}

static void render(int x0, int x1) {          /* pixels x0..x1-1 (0..159) of line ty */
    uint8_t *row = a26_fb + ty * A26_W;
    if (T.vblank & 2) { memset(row + x0, 0, (size_t)(x1 - x0)); return; }
    if (!pfrow_ok) pfrow_rebuild();
    memcpy(row + x0, pfrow + x0, (size_t)(x1 - x0));
    if (odirty) { for (int i = 0; i < 5; i++) if (odirty & (1 << i)) obj_rebuild(i); odirty = 0; }
    uint32_t rm[5], u[5], any = 0;           /* the pixels of this span, and those with an object on them */
    for (int w = 0; w < 5; w++) {
        int lo = x0 - w * 32, hi = x1 - w * 32;
        uint32_t m;
        if (hi <= 0 || lo >= 32) m = 0;
        else { m = 0xFFFFFFFFu; if (lo > 0) m &= ~((1u << lo) - 1); if (hi < 32) m &= (1u << hi) - 1; }
        rm[w] = m;
        u[w] = (omask[0][w] | omask[1][w] | omask[2][w] | omask[3][w] | omask[4][w]) & m;
        any |= u[w];
    }
    if (any) {
        uint32_t c[16] = { 0 };               /* which pairs touch: P0P1 M0M1 M0P0 M0P1 M1P0 M1P1 P0PF P0BL P1PF P1BL M0PF M0BL M1PF M1BL BLPF */
        for (int w = 0; w < 5; w++) {
            uint32_t p0 = omask[0][w] & rm[w], p1 = omask[1][w] & rm[w], m0 = omask[2][w] & rm[w], m1 = omask[3][w] & rm[w], bl = omask[4][w] & rm[w];
            uint32_t pf = T.pfmask[w] & rm[w];
            c[0] |= p0 & p1; c[1] |= m0 & m1; c[2] |= m0 & p0; c[3] |= m0 & p1; c[4] |= m1 & p0; c[5] |= m1 & p1;
            c[6] |= p0 & pf; c[7] |= p0 & bl; c[8] |= p1 & pf; c[9] |= p1 & bl; c[10] |= m0 & pf; c[11] |= m0 & bl;
            c[12] |= m1 & pf; c[13] |= m1 & bl; c[14] |= bl & pf;
        }
        uint64_t cx = 0;
        if (c[2]) cx |= 0x40ull;          if (c[3]) cx |= 0x80ull;                 /* CXM0P */
        if (c[4]) cx |= 0x80ull << 8;     if (c[5]) cx |= 0x40ull << 8;            /* CXM1P */
        if (c[6]) cx |= 0x80ull << 16;    if (c[7]) cx |= 0x40ull << 16;           /* CXP0FB */
        if (c[8]) cx |= 0x80ull << 24;    if (c[9]) cx |= 0x40ull << 24;           /* CXP1FB */
        if (c[10]) cx |= 0x80ull << 32;   if (c[11]) cx |= 0x40ull << 32;          /* CXM0FB */
        if (c[12]) cx |= 0x80ull << 40;   if (c[13]) cx |= 0x40ull << 40;          /* CXM1FB */
        if (c[14]) cx |= 0x80ull << 48;                                            /* CXBLPF */
        if (c[0]) cx |= 0x80ull << 56;    if (c[1]) cx |= 0x40ull << 56;           /* CXPPMM */
        T.cx |= cx;
        const uint8_t c0 = T.colup[0] >> 1, c1 = T.colup[1] >> 1, cpf = T.colupf >> 1, cbk = T.colubk >> 1;
        const int score = T.ctrlpf & 2, prio = T.ctrlpf & 4;
        for (int w = 0; w < 5; w++) {
            uint32_t bits = u[w];
            while (bits) {
                int b = __builtin_ctz(bits); bits &= bits - 1;
                int x = w * 32 + b;
                int p0 = (omask[0][w] >> b) & 1, p1 = (omask[1][w] >> b) & 1, m0 = (omask[2][w] >> b) & 1, m1 = (omask[3][w] >> b) & 1;
                int bl = (omask[4][w] >> b) & 1, pf = (T.pfmask[w] >> b) & 1;
                uint8_t col;
                if (prio) {
                    if (pf) col = score ? (x < 80 ? c0 : c1) : cpf;
                    else if (bl) col = cpf;
                    else if (p0 | m0) col = c0;
                    else col = c1;
                } else {
                    if (p0 | m0) col = c0;
                    else if (p1 | m1) col = c1;
                    else if (bl) col = cpf;
                    else if (pf) col = score ? (x < 80 ? c0 : c1) : cpf;
                    else col = cbk;
                }
                row[x] = col;
            }
        }
    }
    if (hmove_blank && x0 < 8) memset(row + x0, 0, (size_t)((x1 < 8 ? x1 : 8) - x0));
}

static void lock_missiles(void) {             /* RESMPx: a missile sits in the middle of its player */
    for (int i = 0; i < 2; i++) if (T.resmp[i] & 2) {
        int t = T.nusiz[i] & 7, off = t == 5 ? 7 : t == 7 ? 15 : 3;
        T.pos[2 + i] = (T.pos[i] + off) % 160; odirty |= 4u << i;
    }
}
static void tia_sync(uint32_t to) {
    uint32_t d = (to - tia_cyc) * 3; tia_cyc = to;
    while (d) {
        int run = 228 - tx; if ((uint32_t)run > d) run = (int)d;
        if (tx + run > 68 && ty < A26_MAXLINES) { int a = tx < 68 ? 0 : tx - 68; render(a, tx + run - 68); }
        tx += run; d -= (uint32_t)run;
        if (tx == 228) {
            tx = 0; hmove_blank = 0; ty++;
            if (T.resmp[0] | T.resmp[1]) lock_missiles();
            if (ty >= A26_MAXLINES) { frame_done = 1; a26_lines = ty; ty = 0; }   /* no VSYNC at all */
        }
    }
}

/* ---- sound: two channels, ticked every 38 CPU cycles (31.4 kHz) */
static struct { uint8_t c, f, v; int cnt, idx; uint8_t p4, p5, out; uint32_t p9; int pulse; } ach[2];
static uint32_t aud_cyc; static int16_t ticks[1100]; static int nticks;
static void ch_reset(int i) { ach[i].p4 = 15; ach[i].p5 = 31; ach[i].p9 = 511; ach[i].out = 1; ach[i].idx = 0; ach[i].cnt = 0; ach[i].pulse = 0; }
static void p4step(int i) { uint8_t s = ach[i].p4; ach[i].p4 = (uint8_t)(((s << 1) | (((s >> 3) ^ (s >> 2)) & 1)) & 15); }
static void p5step(int i) { uint8_t s = ach[i].p5; ach[i].p5 = (uint8_t)(((s << 1) | (((s >> 4) ^ (s >> 2)) & 1)) & 31); }
static void p9step(int i) { uint32_t s = ach[i].p9; ach[i].p9 = ((s << 1) | (((s >> 8) ^ (s >> 4)) & 1)) & 511; }
static void ch_pulse(int i) {                 /* the divider fired: advance the waveform */
    switch (ach[i].c & 15) {
    case 0: case 11: ach[i].out = 1; break;
    case 1: p4step(i); ach[i].out = ach[i].p4 & 1; break;
    case 2: if (++ach[i].pulse >= 15) { ach[i].pulse = 0; p4step(i); } ach[i].out = ach[i].p4 & 1; break;
    case 3: p5step(i); if (ach[i].p5 & 1) p4step(i); ach[i].out = ach[i].p4 & 1; break;
    case 4: case 5: ach[i].out ^= 1; break;
    case 6: case 10: ach[i].idx = (ach[i].idx + 1) % 31; ach[i].out = ach[i].idx >= 13; break;
    case 7: case 9: p5step(i); ach[i].out = ach[i].p5 & 1; break;
    case 8: p9step(i); ach[i].out = ach[i].p9 & 1; break;
    case 12: case 13: ach[i].idx = (ach[i].idx + 1) % 6; ach[i].out = ach[i].idx >= 3; break;
    case 14: ach[i].idx = (ach[i].idx + 1) % 93; ach[i].out = ach[i].idx >= 39; break;
    case 15: if (++ach[i].pulse >= 6) { ach[i].pulse = 0; p5step(i); } ach[i].out = ach[i].p5 & 1; break;
    }
}
static void aud_sync(uint32_t to) {
    while ((int32_t)(to - aud_cyc) >= 38) {
        aud_cyc += 38;
        for (int i = 0; i < 2; i++) if (--ach[i].cnt < 0) { ach[i].cnt = ach[i].f & 31; ch_pulse(i); }
        int s = (ach[0].out ? ach[0].v : 0) + (ach[1].out ? ach[1].v : 0);
        if (nticks < (int)(sizeof ticks / sizeof ticks[0])) ticks[nticks++] = (int16_t)(s * 900);
    }
}

static void wsync_stall(void);
static int wsync_req;
static void tia_write(unsigned r, uint8_t v) {
    switch (r) {
    case 0x00: {
        int on = (v & 2) != 0;
#ifdef A26_TRACE
        { extern int printf(const char *, ...); printf("VSYNC=%02x ty=%d tx=%d cyc=%u\n", v, ty, tx, cyc); }
#endif
        if (on && !T.vsync) { if (ty > 20) { a26_lines = ty; frame_done = 1; } }
        if (!on && T.vsync) { ty = 0; }
        T.vsync = (uint8_t)on; break; }
    case 0x01: T.vblank = v; break;
    case 0x02: wsync_req = 1; break;
    case 0x03: tx = 0; hmove_blank = 0; break;            /* RSYNC (rare): the line restarts here */
    case 0x04: case 0x05: T.nusiz[r - 4] = v; odirty |= (1u << (r - 4)) | (4u << (r - 4)); break;
    case 0x06: case 0x07: T.colup[r - 6] = v; pfrow_ok = 0; break;
    case 0x08: T.colupf = v; pfrow_ok = 0; break;
    case 0x09: T.colubk = v; pfrow_ok = 0; break;
    case 0x0A: T.ctrlpf = v; pf_rebuild(); odirty |= 0x10; break;
    case 0x0B: case 0x0C: T.refp[r - 0x0B] = v; odirty |= 1u << (r - 0x0B); break;
    case 0x0D: T.pf[0] = v; pf_rebuild(); break;
    case 0x0E: T.pf[1] = v; pf_rebuild(); break;
    case 0x0F: T.pf[2] = v; pf_rebuild(); break;
    case 0x10: case 0x11: T.pos[r - 0x10] = tx < 68 ? 3 : (tx - 68 + 5) % 160; odirty |= (1u << (r - 0x10)) | (4u << (r - 0x10)); break;
    case 0x12: case 0x13: T.pos[r - 0x10] = tx < 68 ? 2 : (tx - 68 + 4) % 160; odirty |= 1u << (r - 0x10); break;
    case 0x14: T.pos[4] = tx < 68 ? 2 : (tx - 68 + 4) % 160; odirty |= 0x10; break;
    case 0x15: case 0x16: aud_sync(cyc); ach[r - 0x15].c = v & 15; break;
    case 0x17: case 0x18: aud_sync(cyc); ach[r - 0x17].f = v & 31; break;
    case 0x19: case 0x1A: aud_sync(cyc); ach[r - 0x19].v = v & 15; break;
    case 0x1B: T.grp[0] = v; T.grpold[1] = T.grp[1]; odirty |= 3; break;
    case 0x1C: T.grp[1] = v; T.grpold[0] = T.grp[0]; T.enablold = T.enabl; odirty |= 0x13; break;
    case 0x1D: case 0x1E: T.enam[r - 0x1D] = v; odirty |= 4u << (r - 0x1D); break;
    case 0x1F: T.enabl = v; odirty |= 0x10; break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: T.hm[r - 0x20] = v; break;
    case 0x25: case 0x26: T.vdelp[r - 0x25] = v & 1; odirty |= 1u << (r - 0x25); break;
    case 0x27: T.vdelbl = v & 1; odirty |= 0x10; break;
    case 0x28: case 0x29: {
        int i = (int)(r - 0x28);
        T.resmp[i] = v; odirty |= 4u << i;
        if (!(v & 2)) { T.resmp[i] = 0; }
        else lock_missiles();
        break; }
    case 0x2A:
        for (int i = 0; i < 5; i++) {
            int sm = ((T.hm[i] >> 4) ^ 8) - 8;          /* +7 (0x70): 7 pixels left ... -8 (0x80): 8 right */
            T.pos[i] = ((T.pos[i] - sm) % 160 + 160) % 160;
        }
        odirty = 0x1F;
        if (tx < 76) hmove_blank = 1;
        break;
    case 0x2B: memset(T.hm, 0, sizeof T.hm); break;
    case 0x2C: T.cx = 0; break;
    }
}
static uint8_t tia_read(unsigned r) {
    switch (r & 15) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: return (uint8_t)(T.cx >> ((r & 7) * 8));
    case 0xC: return inpt4;
    case 0xD: return inpt5;
    default: return 0;
    }
}

/* ------------------------------------------------------------------------------------------------ CPU + bus */
#ifdef A26_FLAT_TEST
uint8_t flat[65536];
static inline uint8_t rd(uint16_t a) { cyc++; return flat[a]; }
static inline void wr(uint16_t a, uint8_t v) { cyc++; flat[a] = v; }
#else
static uint8_t rd(uint16_t a) {
    uint8_t v; a &= 0x1FFF;
    if (a & 0x1000) v = cart_rd(a & 0xFFF);
    else if (!(a & 0x80)) { tia_sync(cyc); v = tia_read(a); }
    else if (!(a & 0x200)) v = ram[a & 0x7F];
    else v = riot_read(a);
    cyc++; return v;
}
static void wr(uint16_t a, uint8_t v) {
    a &= 0x1FFF;
    if (a & 0x1000) cart_wr(a & 0xFFF, v);
    else if (!(a & 0x80)) {
        tia_sync(cyc); tia_write(a & 0x3F, v);
        if (mapper == M_3F && a < 0x40) set_3f(v);
    }
    else if (!(a & 0x200)) ram[a & 0x7F] = v;
    else riot_write(a, v);
    cyc++;
    if (wsync_req) wsync_stall();
}
static void wsync_stall(void) {               /* RDY low: the CPU waits for the end of the line */
    wsync_req = 0;
    tia_sync(cyc);
    if (tx) cyc += (uint32_t)(228 - tx) / 3;
}
#endif
static inline void tick(void) { cyc++; }

static uint8_t A, X, Y, S, N, V, Z, C, D, I;  /* N: 0/0x80 ; V Z C D I: 0/1 */
static uint16_t PC;
static inline void setnz(uint8_t v) { N = v & 0x80; Z = !v; }
static inline uint8_t getp(int b) { return (uint8_t)(N | (V << 6) | 0x20 | (b << 4) | (D << 3) | (I << 2) | (Z << 1) | C); }
static inline void setp(uint8_t p) { N = p & 0x80; V = (p >> 6) & 1; D = (p >> 3) & 1; I = (p >> 2) & 1; Z = (p >> 1) & 1; C = p & 1; }
static inline void push(uint8_t v) { wr(0x100 + S, v); S--; }
static inline uint8_t pull(void) { S++; return rd(0x100 + S); }

static uint16_t ea(int mode, int w) {
    uint16_t a, b, r;
    switch (mode) {
    case 0: b = rd(PC++); tick(); b = (uint8_t)(b + X); a = rd(b); a |= (uint16_t)(rd((uint8_t)(b + 1)) << 8); return a;
    case 1: return rd(PC++);
    case 2: return PC++;
    case 3: a = rd(PC++); a |= (uint16_t)(rd(PC++) << 8); return a;
    case 4: b = rd(PC++); a = rd(b); a |= (uint16_t)(rd((uint8_t)(b + 1)) << 8); r = (uint16_t)(a + Y);
            if (w || ((a ^ r) & 0xFF00)) tick(); return r;
    case 5: b = rd(PC++); tick(); return (uint8_t)(b + X);
    case 6: a = rd(PC++); a |= (uint16_t)(rd(PC++) << 8); r = (uint16_t)(a + Y);
            if (w || ((a ^ r) & 0xFF00)) tick(); return r;
    case 7: a = rd(PC++); a |= (uint16_t)(rd(PC++) << 8); r = (uint16_t)(a + X);
            if (w || ((a ^ r) & 0xFF00)) tick(); return r;
    default: b = rd(PC++); tick(); return (uint8_t)(b + Y);      /* 8: zero page, Y */
    }
}
static void adc(uint8_t m) {
    if (!D) {
        unsigned s = A + m + C; V = ((~(A ^ m) & (A ^ s)) & 0x80) != 0; C = s > 0xFF; A = (uint8_t)s; setnz(A);
    } else {                                  /* NMOS decimal mode */
        unsigned bin = A + m + C; Z = !(bin & 0xFF);
        int al = (A & 15) + (m & 15) + C; if (al >= 10) al = ((al + 6) & 15) + 16;
        int t = (A & 0xF0) + (m & 0xF0) + al;
        N = t & 0x80; V = ((~(A ^ m) & (A ^ t)) & 0x80) != 0;
        if (t >= 0xA0) t += 0x60;
        C = t >= 0x100; A = (uint8_t)t;
    }
}
static void sbc(uint8_t m) {
    unsigned s = A + (uint8_t)~m + C;
    uint8_t bv = (uint8_t)(((A ^ m) & (A ^ s) & 0x80) != 0);
    if (!D) { V = bv; C = s > 0xFF; A = (uint8_t)s; setnz(A); return; }
    int al = (A & 15) - (m & 15) + C - 1; if (al < 0) al = ((al - 6) & 15) - 16;
    int t = (A & 0xF0) - (m & 0xF0) + al; if (t < 0) t -= 0x60;
    V = bv; C = s > 0xFF; setnz((uint8_t)s); A = (uint8_t)t;
}
static void cmp(uint8_t r, uint8_t m) { C = r >= m; setnz((uint8_t)(r - m)); }
static uint8_t shift(int k, uint8_t v) {      /* 0 ASL 1 ROL 2 LSR 3 ROR */
    uint8_t c = C;
    switch (k) {
    case 0: C = v >> 7; v <<= 1; break;
    case 1: C = v >> 7; v = (uint8_t)((v << 1) | c); break;
    case 2: C = v & 1; v >>= 1; break;
    default: C = v & 1; v = (uint8_t)((v >> 1) | (c << 7)); break;
    }
    setnz(v); return v;
}
static void alu(int k, uint8_t m) {           /* 0 ORA 1 AND 2 EOR 3 ADC 5 LDA 6 CMP 7 SBC */
    switch (k) {
    case 0: A |= m; setnz(A); break;
    case 1: A &= m; setnz(A); break;
    case 2: A ^= m; setnz(A); break;
    case 3: adc(m); break;
    case 5: A = m; setnz(A); break;
    case 6: cmp(A, m); break;
    case 7: sbc(m); break;
    }
}
static void branch(int taken) {
    int8_t off = (int8_t)rd(PC++);
    if (taken) { tick(); uint16_t n = (uint16_t)(PC + off); if ((n ^ PC) & 0xFF00) tick(); PC = n; }
}

static void cpu_step(void) {
    uint8_t op = rd(PC++);
    int cc = op & 3, bbb = (op >> 2) & 7, aaa = op >> 5;
    uint16_t a; uint8_t m;
    switch (op) {
    case 0x00: rd(PC++); push((uint8_t)(PC >> 8)); push((uint8_t)PC); push(getp(1)); I = 1;
               a = rd(0xFFFE); a |= (uint16_t)(rd(0xFFFF) << 8); PC = a; return;
    case 0x08: tick(); push(getp(1)); return;
    case 0x18: tick(); C = 0; return;
    case 0x20: a = rd(PC++); tick(); push((uint8_t)(PC >> 8)); push((uint8_t)PC); a |= (uint16_t)(rd(PC) << 8); PC = a; return;
    case 0x28: tick(); tick(); setp(pull()); return;
    case 0x38: tick(); C = 1; return;
    case 0x40: tick(); tick(); setp(pull()); a = pull(); a |= (uint16_t)(pull() << 8); PC = a; return;
    case 0x48: tick(); push(A); return;
    case 0x4C: a = rd(PC++); a |= (uint16_t)(rd(PC) << 8); PC = a; return;
    case 0x58: tick(); I = 0; return;
    case 0x60: tick(); tick(); a = pull(); a |= (uint16_t)(pull() << 8); tick(); PC = (uint16_t)(a + 1); return;
    case 0x68: tick(); tick(); A = pull(); setnz(A); return;
    case 0x6C: { uint16_t p = rd(PC++); p |= (uint16_t)(rd(PC) << 8);
                 a = rd(p); a |= (uint16_t)(rd((uint16_t)((p & 0xFF00) | ((p + 1) & 0xFF))) << 8); PC = a; return; }
    case 0x78: tick(); I = 1; return;
    case 0x88: tick(); Y--; setnz(Y); return;
    case 0x8A: tick(); A = X; setnz(A); return;
    case 0x98: tick(); A = Y; setnz(A); return;
    case 0x9A: tick(); S = X; return;
    case 0xA8: tick(); Y = A; setnz(Y); return;
    case 0xAA: tick(); X = A; setnz(X); return;
    case 0xB8: tick(); V = 0; return;
    case 0xBA: tick(); X = S; setnz(X); return;
    case 0xC8: tick(); Y++; setnz(Y); return;
    case 0xCA: tick(); X--; setnz(X); return;
    case 0xD8: tick(); D = 0; return;
    case 0xE8: tick(); X++; setnz(X); return;
    case 0xEA: tick(); return;
    case 0xF8: tick(); D = 1; return;
    case 0x10: branch(!N); return;
    case 0x30: branch(N != 0); return;
    case 0x50: branch(!V); return;
    case 0x70: branch(V); return;
    case 0x90: branch(!C); return;
    case 0xB0: branch(C); return;
    case 0xD0: branch(!Z); return;
    case 0xF0: branch(Z); return;
    }
    if (cc == 1) {
        if (aaa == 4) { if (bbb == 2) { rd(ea(2, 0)); return; } a = ea(bbb, 1); wr(a, A); return; }
        a = ea(bbb, 0); alu(aaa, rd(a)); return;
    }
    if (cc == 2) {
        if (bbb == 2) {                       /* accumulator shifts; the other implied ones were handled above */
            tick(); if (aaa < 4) A = shift(aaa, A); return;
        }
        if (bbb == 0) { if (aaa == 5) { X = rd(ea(2, 0)); setnz(X); } else rd(ea(2, 0)); return; }
        if (bbb == 4 || bbb == 6) { tick(); return; }          /* (jam / nop: not used) */
        int mode = bbb; if ((aaa == 4 || aaa == 5) && bbb == 5) mode = 8; if (aaa == 5 && bbb == 7) mode = 6;
        if (aaa == 4) { a = ea(mode, 1); wr(a, X); return; }
        if (aaa == 5) { a = ea(mode, 0); X = rd(a); setnz(X); return; }
        a = ea(mode, 1); m = rd(a); tick();
        if (aaa < 4) m = shift(aaa, m); else { m = (uint8_t)(m + (aaa == 7 ? 1 : -1)); setnz(m); }
        wr(a, m); return;
    }
    if (cc == 0) {
        if (bbb == 0 || bbb == 1 || bbb == 3 || bbb == 5 || bbb == 7) {
            if (aaa == 4) { if (bbb == 0) { rd(ea(2, 0)); return; } a = ea(bbb, 1); wr(a, Y); return; }
            a = ea(bbb == 0 ? 2 : bbb, 0); m = rd(a);
            switch (aaa) {
            case 1: Z = !(A & m); N = m & 0x80; V = (m >> 6) & 1; break;                      /* BIT */
            case 5: Y = m; setnz(Y); break;
            case 6: cmp(Y, m); break;
            case 7: cmp(X, m); break;
            }
            return;
        }
        tick(); return;
    }
    /* cc == 3: the common undocumented opcodes */
    if (bbb == 2) { if (op == 0xEB) { sbc(rd(ea(2, 0))); return; } rd(ea(2, 0)); return; }
    if (aaa == 4) {                           /* SAX */
        if (bbb == 7 || bbb == 6 || bbb == 4) { tick(); return; }
        a = ea(bbb == 5 ? 8 : bbb, 1); wr(a, A & X); return;
    }
    if (aaa == 5) {                           /* LAX */
        a = ea(bbb == 5 ? 8 : bbb == 7 ? 6 : bbb, 0); A = X = rd(a); setnz(A); return;
    }
    a = ea(bbb, 1); m = rd(a); tick();
    switch (aaa) {
    case 0: m = shift(0, m); A |= m; setnz(A); break;                       /* SLO */
    case 1: m = shift(1, m); A &= m; setnz(A); break;                       /* RLA */
    case 2: m = shift(2, m); A ^= m; setnz(A); break;                       /* SRE */
    case 3: m = shift(3, m); adc(m); break;                                 /* RRA */
    case 6: m--; cmp(A, m); break;                                          /* DCP */
    case 7: m++; sbc(m); break;                                             /* ISB */
    }
    wr(a, m);
}

#ifdef A26_FLAT_TEST
void a26_reset(void) {}
void a26_flat_init(uint16_t pc) { A = X = Y = 0; S = 0xFD; PC = pc; N = V = Z = C = D = 0; I = 1; cyc = 0; }
uint16_t a26_flat_step(void) { cpu_step(); return PC; }
#else
/* ------------------------------------------------------------------------------------------------ public */
void a26_reset(void) {
    memset(ram, 0, sizeof ram); memset(&T, 0, sizeof T); memset(ach, 0, sizeof ach);
    cxinit(); tables_init(); odirty = 0x1F; pfrow_ok = 0; pf_rebuild();
    ch_reset(0); ch_reset(1);
    cyc = tia_cyc = aud_cyc = 0; tx = ty = 0; hmove_blank = 0; frame_done = 0; nticks = 0; wsync_req = 0;
    t_start = 0; t_rem0 = 0x3FF * 1024; t_int = 1024;
    memset(a26_fb, 0, sizeof a26_fb);
    cart_reset();
    A = X = Y = 0; S = 0xFD; N = V = Z = C = D = 0; I = 1;
    uint16_t a = cart_rd(0xFFC); a |= (uint16_t)(cart_rd(0xFFD) << 8); PC = a;
}
void a26_frame(void) {
    frame_done = 0; nticks = 0;
    uint32_t guard = cyc + 80000;             /* (a program that never ends a picture: stop after ~4 frames' time) */
    while (!frame_done && (int32_t)(guard - cyc) > 0) cpu_step();
    tia_sync(cyc); aud_sync(cyc);
}
void a26_input(unsigned j0, unsigned j1, int reset, int select, int bw, int d0a, int d1a) {
    unsigned a = 0xFF;
    if (j0 & A26_UP) a &= ~0x10u; if (j0 & A26_DOWN) a &= ~0x20u; if (j0 & A26_LEFT) a &= ~0x40u; if (j0 & A26_RIGHT) a &= ~0x80u;
    if (j1 & A26_UP) a &= ~0x01u; if (j1 & A26_DOWN) a &= ~0x02u; if (j1 & A26_LEFT) a &= ~0x04u; if (j1 & A26_RIGHT) a &= ~0x08u;
    swcha = (uint8_t)a;
    inpt4 = (j0 & A26_FIRE) ? 0x00 : 0x80; inpt5 = (j1 & A26_FIRE) ? 0x00 : 0x80;
    uint8_t b = 0x34;                         /* unused bits read 1 */
    if (!reset) b |= 1; if (!select) b |= 2; if (!bw) b |= 8; if (!d0a) b |= 0x40; if (!d1a) b |= 0x80;
    swchb = b;
}
int a26_audio(int16_t *out, int max) {
    static uint32_t frac; static int dc;
    int n = nticks; if (!n) return 0;
    int m = (int)(((uint64_t)n * 48000 + frac) / 31399); frac = (uint32_t)(((uint64_t)n * 48000 + frac) % 31399);
    if (m > max) m = max;
    for (int j = 0; j < m; j++) {
        int i0 = (int)((long)j * n / m), i1 = (int)((long)(j + 1) * n / m); if (i1 <= i0) i1 = i0 + 1; if (i1 > n) i1 = n;
        int s = 0; for (int i = i0; i < i1; i++) s += ticks[i]; s /= (i1 - i0);
        dc += (s - dc) >> 9; s -= dc;
        out[j] = (int16_t)(s > 32000 ? 32000 : s < -32000 ? -32000 : s);
    }
    return m;
}
uint32_t a26_cycles(void) { return cyc; }
void a26_cpu_regs(uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *sp, uint8_t *p) { *pc = PC; *a = A; *x = X; *y = Y; *sp = S; *p = getp(0); }

uint32_t a26_palette(int index) {             /* index = colour >> 1: hue = index >> 3 (0..15), luminance = index & 7 (NTSC) */
static const uint32_t pal[128] = {
    0x000000, 0x4a4a4a, 0x6f6f6f, 0x8e8e8e, 0xaaaaaa, 0xc0c0c0, 0xd6d6d6, 0xececec,
    0x484800, 0x69690f, 0x86861d, 0xa2a22a, 0xbbbb35, 0xd2d240, 0xe8e84a, 0xfcfc54,
    0x7c2c00, 0x904811, 0xa26221, 0xb47a30, 0xc3903d, 0xd2a44a, 0xdfb755, 0xecc860,
    0x901c00, 0xa33915, 0xb55328, 0xc66c3a, 0xd5824a, 0xe39759, 0xf0aa67, 0xfcba74,
    0x940000, 0xa71a1a, 0xb83232, 0xc84848, 0xd65c5c, 0xe46f6f, 0xf08080, 0xfc9090,
    0x840064, 0x97197a, 0xa8308f, 0xb846a2, 0xc659b3, 0xd46cc3, 0xe07cd2, 0xec8ce0,
    0x500084, 0x68199a, 0x7d30ad, 0x9246c0, 0xa459d0, 0xb56ce0, 0xc47cee, 0xd48cfc,
    0x140090, 0x331aa3, 0x4e32b5, 0x6848c6, 0x7f5cd5, 0x956fe3, 0xa980f0, 0xbc90fc,
    0x000094, 0x181aa7, 0x2d32b8, 0x4248c8, 0x545cd6, 0x656fe4, 0x7580f0, 0x8490fc,
    0x001c88, 0x183b9d, 0x2d57b0, 0x4272c2, 0x548ad2, 0x65a0e1, 0x75b5ef, 0x84c8fc,
    0x003064, 0x185080, 0x2d6d98, 0x4288b0, 0x54a0c5, 0x65b7d9, 0x75cceb, 0x84e0fc,
    0x004030, 0x18624e, 0x2d8169, 0x429e82, 0x54b899, 0x65d1ae, 0x75e7c2, 0x84fcd4,
    0x004400, 0x1a661a, 0x328432, 0x48a048, 0x5cba5c, 0x6fd26f, 0x80e880, 0x90fc90,
    0x143c00, 0x355f18, 0x527e2d, 0x6e9c42, 0x87b754, 0x9ed065, 0xb4e775, 0xc8fc84,
    0x303800, 0x505916, 0x6d762b, 0x88923e, 0xa0ab4f, 0xb7c25f, 0xccd86e, 0xe0ec7c,
    0x482c00, 0x694d14, 0x866a26, 0xa28638, 0xbb9f47, 0xd2b656, 0xe8cc63, 0xfce070,
};
    return pal[index & 127];
}
#endif
