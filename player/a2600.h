/* A small Atari 2600 core: 6507 CPU, TIA (video + sound), RIOT (RAM, timer, ports), cartridges up to 32 KB
 * (2K, 4K, F8, F6, F4, FA, E0, 3F; "super chip" RAM). Portable C, no dependencies: the GamePad player and the PC test
 * harness use the same file. One machine at a time (the state is static). */
#pragma once
#include <stdint.h>

#define A26_W 160                 /* pixels a line */
#define A26_MAXLINES 320          /* lines kept (NTSC 262, PAL 312) */
extern uint8_t a26_fb[A26_MAXLINES * A26_W];   /* (4-byte aligned) */   /* the picture: colour >> 1 (0..127) = index into a26_palette() */
extern int a26_lines;             /* lines the last finished frame had (262 NTSC, 312 PAL) */

/* rom: the cartridge's bytes (kept, not copied). 0 = ok; < 0 = size / format not supported */
int  a26_load(const uint8_t *rom, int size);
const char *a26_mapper_name(void);
void a26_reset(void);
void a26_frame(void);             /* runs until the TIA has finished a picture (VSYNC) */
/* controls: 1 = pressed. joystick 0: up down left right fire; console: reset select; switches */
void a26_input(unsigned joy0, unsigned joy1, int reset, int select, int bw, int diff0_a, int diff1_a);
#define A26_UP 1
#define A26_DOWN 2
#define A26_LEFT 4
#define A26_RIGHT 8
#define A26_FIRE 16
/* the 128 colours of the picture as 0xRRGGBB (NTSC-like) */
uint32_t a26_palette(int index);
/* the sound of the frame just run: mono 16-bit at 48 kHz; returns the number of samples (<= max) */
int  a26_audio(int16_t *out, int max);
/* 64-bit CPU cycle counter (for tests) and a few registers (for tests) */
uint32_t a26_cycles(void);
void a26_cpu_regs(uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y, uint8_t *sp, uint8_t *p);
