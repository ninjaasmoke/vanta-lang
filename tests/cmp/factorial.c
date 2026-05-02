/* factorial(10) = 3628800. Take it mod 10000 so it fits in an exit code.
 * Same recursive shape as the .vt sibling.
 */

static int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int main(void) {
    return fact(10) % 10000;   /* 8800 */
}
