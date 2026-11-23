/* Puts the TIA's picture on the GamePad's 8-bit layer: 160 px -> 800 px (5x), 2 rows a line, only the lines that changed. */
#pragma once
#include <stdint.h>
void present_rows(int y, int h);                          /* provided by the caller: show rows y..y+h of the layer */
void draw_init(uint8_t *fb8, uint32_t stride, int w, int h);
/* lines: the picture's lines (160 bytes each), first/n: the window to show; returns the rows presented */
int draw_frame(const uint8_t *fb, int first, int n);
void draw_reset(void);                                    /* the next draw_frame repaints everything */
