/* The Atari 2600 player for the Wii U GamePad (libwup): a2600.c runs the cartridge, this file is the pad around it.
 * Started by the launcher (../main.c) with a ROM's path; HOME goes back to the launcher.
 *
 *   Picture:  the TIA's 160 x ~210 pixels in the firmware's 8-bit layer, 5 x 2 times bigger (800 x 420) in the middle of the screen.
 *   Sound:    the TIA's two voices, 48 kHz mono on both speakers.
 *   Controls: D-pad or left stick  joystick     A, B or ZR  fire      + Reset     - Select
 *             Y  colour / black and white      L  left difficulty A/B      R  right difficulty A/B      HOME  leave */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wup.h"
#include "fs.h"
#include "a2600.h"
#include "draw.h"

WUP_APP("Atari 2600 Player");

#define W 854
#define H 480
#define DEAD 400

static uint8_t *fb8; static uint32_t stride;
void present_rows(int y, int h) { wup_present8(y, h); }

static void back_to_launcher(void) {
    char p[96];
    snprintf(p, sizeof p, "%s/atari.wup", wup_app_dir()[0] ? wup_app_dir() : "/apps/atari");
    wup_exec(p, NULL);
}

static void draw(void) {
    int pal = a26_lines > 290;                          /* PAL: 312 lines, the picture starts lower and is taller */
    draw_frame(a26_fb, pal ? 40 : 34, pal ? 228 : 210);
}

int main(void) {
    const char *path = wup_arg();
    fb8 = wup_screen8(); stride = wup_stride8();
    for (int i = 0; i < 128; i++) wup_palette(i, a26_palette(i));
    wup_palette(128, 0);
    draw_init(fb8, stride, W, H);
    if (!path || !path[0]) { printf("[atari] started without a ROM\n"); back_to_launcher(); return 0; }

    int size = fs_filesize(path);
    static const int ok[] = { 32768, 16384, 12288, 8192, 4096, 2048 };
    int use = 0;
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) if (size >= ok[i]) { use = ok[i]; break; }   /* (a header or padding after the ROM is ignored) */
    uint8_t *rom = use ? malloc((size_t)use) : NULL;
    if (!rom || fs_read(path, 0, rom, (uint32_t)use) != use || a26_load(rom, use) < 0) {
        printf("[atari] cannot load %s (%d bytes)\n", path, size);
        wup_sleep(1500); back_to_launcher(); return 0;
    }
    printf("[atari] %s: %d bytes, mapper %s\n", path, use, a26_mapper_name());
    wup_audio_open();

    int bw = 0, d0 = 1, d1 = 1;
    unsigned prev = 0; int nf = 0, skipped = 0, nskip = 0, have = 0, pc = 0; uint32_t te = 0, td = 0;
    static int16_t mono[1000], st[2000];
    uint32_t last_v = wup_us();                           /* when the last refresh started */
    /* One picture a refresh. The firmware's layer is a single buffer that the display reads as we write: to avoid a tear, the
     * picture emulated during the last refresh is drawn right after the vertical blank (top to bottom, faster than the beam),
     * and the next one is emulated for the rest of the refresh. When emulating takes longer than a refresh, the game keeps its
     * speed: we do not wait, and the pictures are not drawn (3 in a row at most). */
    for (;;) {
        uint32_t now = wup_us();
        int waited = 0;
        if ((int32_t)(now - last_v) <= 14500) {
            wup_vblank(); waited = 1;
            if (a26_lines > 290 && ++pc >= 5) { pc = 0; wup_vblank(); }      /* PAL: 50 Hz = 5 pictures in 6 refreshes */
            last_v = wup_us();
        } else last_v = now;                              /* late: no waiting */
        if (have) {
            if (waited || skipped >= 3) {
                uint32_t t = wup_us(); draw(); td += wup_us() - t; skipped = 0;
            } else { skipped++; nskip++; }
        }
        unsigned b = wup_buttons();
        if (b & WUP_HOME) break;
        unsigned pressed = b & ~prev; prev = b;
        if (pressed & WUP_Y) bw = !bw;
        if (pressed & WUP_L) d0 = !d0;
        if (pressed & WUP_R) d1 = !d1;
        int lx = wup_stick(0, 0), ly = wup_stick(0, 1);
        unsigned j = 0;
        if (ly > DEAD || (b & WUP_UP)) j |= A26_UP;
        if (ly < -DEAD || (b & WUP_DOWN)) j |= A26_DOWN;
        if (lx < -DEAD || (b & WUP_LEFT)) j |= A26_LEFT;
        if (lx > DEAD || (b & WUP_RIGHT)) j |= A26_RIGHT;
        if (b & (WUP_A | WUP_B | WUP_ZR | WUP_ZL)) j |= A26_FIRE;
        a26_input(j, 0, !!(b & WUP_PLUS), !!(b & WUP_MINUS), bw, d0, d1);
        uint32_t t0 = wup_us();
        a26_frame(); have = 1;
        te += wup_us() - t0;
        if (++nf == 120) {
            printf("[atari] per picture: emulation %u us, drawing %u us, %d of 120 not drawn (60 Hz = 16667 us)\n",
                   (unsigned)(te / 120), (unsigned)(td / 120), nskip);
            te = td = 0; nf = 0; nskip = 0;
        }
        int m = a26_audio(mono, 1000);
        if (m > 0 && wup_audio_queued() < 6000) {         /* (more than 125 ms waiting: the sound is behind, drop this frame's) */
            for (int i = 0; i < m; i++) st[2 * i] = st[2 * i + 1] = mono[i];
            wup_audio_queue(st, m);
        }
        wup_sleep(0);                                     /* the firmware's tasks (input, radio) */
    }
    wup_audio_close();
    back_to_launcher();
    return 0;
}
