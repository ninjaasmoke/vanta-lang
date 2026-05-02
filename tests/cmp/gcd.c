/* Euclid's algorithm. exit code = gcd(48, 18) = 6. */

static int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main(void) {
    return gcd(48, 18);
}
