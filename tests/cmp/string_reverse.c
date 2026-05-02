/* Reverse a string in place.
 *
 * 'hello' -> 'olleh'. exit code = first char's ASCII value = 'o' = 111.
 *
 * C wins this one cleanly: char is a first-class type, pointer
 * arithmetic on bytes is the natural way to walk a string, and the
 * standard library has strlen.
 */
#include <string.h>

static void reverse(char *s) {
    int n = strlen(s);
    for (int i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

int main(void) {
    char buf[6] = "hello";
    reverse(buf);
    return (unsigned char)buf[0];   /* 'o' = 111 */
}
