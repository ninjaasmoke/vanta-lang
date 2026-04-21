/* Number of iterations for c=(-0.5, 0.0) before |z| > 2, capped at 100.
 *
 * (-0.5, 0) is well inside the main cardioid -> never escapes ->
 * exit code 100 (the cap).
 *
 * Also tries c=(1.0, 0.0) which escapes at iteration 2 and adds it.
 * 100 + 2 = 102.
 */
static int escape(double cr, double ci, int max_iter) {
    double zr = 0, zi = 0;
    for (int i = 0; i < max_iter; i++) {
        double zr2 = zr * zr - zi * zi + cr;
        double zi2 = 2.0 * zr * zi + ci;
        zr = zr2;
        zi = zi2;
        if (zr * zr + zi * zi > 4.0) return i;
    }
    return max_iter;
}

int main(void) {
    int a = escape(-0.5, 0.0, 100);   /* 100 */
    int b = escape( 1.0, 0.0, 100);   /* 2 */
    return a + b;                     /* 102 */
}
