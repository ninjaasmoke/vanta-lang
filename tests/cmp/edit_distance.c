/* Levenshtein edit distance between "kitten" and "sitting".
 *
 * Classic 2D DP. Answer is 3.
 *   k i t t e n  ->  s i t t i n g
 *   1: k -> s        (substitute)
 *   2: e -> i        (substitute)
 *   3: insert g      (at end)
 */
#include <string.h>

static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

int main(void) {
    const char *a = "kitten";
    const char *b = "sitting";
    int la = (int)strlen(a);
    int lb = (int)strlen(b);

    int dp[16][16];
    for (int i = 0; i <= la; i++) dp[i][0] = i;
    for (int j = 0; j <= lb; j++) dp[0][j] = j;

    for (int i = 1; i <= la; i++) {
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = min3(dp[i - 1][j] + 1,
                            dp[i][j - 1] + 1,
                            dp[i - 1][j - 1] + cost);
        }
    }

    return dp[la][lb];   /* 3 */
}
