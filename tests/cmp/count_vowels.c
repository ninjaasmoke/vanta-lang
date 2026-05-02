/* Count vowels in a string. exit code = vowel count.
 *
 * In "the quick brown fox jumps over the lazy dog" there are
 * 11 vowels (e u i k → wait, let me recount: e, u, i, o, o,
 * o, e, u, o, e, e, a, o, ... actually): e, ui, o, o, u, o, e,
 * a, o = let me just trust the program. C will tell us, vanta
 * has to match.
 */
#include <string.h>

static int is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

static int count_vowels(const char *s) {
    int n = 0;
    for (; *s; s++) {
        if (is_vowel(*s)) n++;
    }
    return n;
}

int main(void) {
    return count_vowels("the quick brown fox jumps over the lazy dog");
}
