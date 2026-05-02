/* In-place uppercase ASCII. exit code = 'H' (from "hello world!" upper'd).
 * 'H' = 72.
 */
static void to_upper(char *s, int n) {
    for (int i = 0; i < n; i++) {
        if (s[i] >= 'a' && s[i] <= 'z') {
            s[i] = s[i] - 'a' + 'A';
        }
    }
}

int main(void) {
    char buf[12] = "hello world";
    to_upper(buf, 11);
    return (unsigned char)buf[0];   /* 'H' = 72 */
}
