/* Sieve of Eratosthenes. counts primes up to (and including) 100.
 *
 * pi(100) = 25.
 *
 * 100-element bool array; mark composites, count survivors above 1.
 */
int main(void) {
    int n = 100;
    char is_composite[101] = {0};   /* index 0..100 */

    for (int i = 2; i * i <= n; i++) {
        if (is_composite[i]) continue;
        for (int j = i * i; j <= n; j += i) {
            is_composite[j] = 1;
        }
    }

    int count = 0;
    for (int i = 2; i <= n; i++) {
        if (!is_composite[i]) count++;
    }
    return count;   /* 25 */
}
