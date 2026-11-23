/* Runs Klaus Dormann's 6502 functional test on the core's CPU (flat 64 KB): success = PC stuck at 0x3469. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
extern uint8_t flat[65536];
void a26_flat_init(uint16_t pc); uint16_t a26_flat_step(void);
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    size_t n = fread(flat, 1, 65536, f); fclose(f);
    a26_flat_init(0x400);
    uint16_t last = 0; unsigned long steps = 0;
    for (;;) {
        uint16_t pc = a26_flat_step(); steps++;
        if (pc == last) { printf("stuck at %04X after %lu steps (%s)\n", pc, steps, pc == 0x3469 ? "SUCCESS" : "FAILURE"); return pc != 0x3469; }
        last = pc; if (steps > 200000000UL) { printf("timeout at %04X\n", pc); return 1; }
    }
}
