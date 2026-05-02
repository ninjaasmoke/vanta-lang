/* Population count: number of 1-bits in an integer.
 *
 * The classic Brian Kernighan trick: x &= x - 1 strips the lowest 1.
 *
 * popcount(0xFF) = 8.
 * Returns total popcount of {0xFF, 0xF0, 0x0F, 0x00, 0xAA, 0x55} =
 *   8 + 4 + 4 + 0 + 4 + 4 = 24.
 */
static int popcount(unsigned x) {
    int c = 0;
    while (x) {
        x &= x - 1;
        c++;
    }
    return c;
}

int main(void) {
    unsigned vals[] = {0xFF, 0xF0, 0x0F, 0x00, 0xAA, 0x55};
    int total = 0;
    for (int i = 0; i < 6; i++) total += popcount(vals[i]);
    return total;   /* 24 */
}
