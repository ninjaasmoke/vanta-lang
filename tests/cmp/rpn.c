/* Reverse-polish calculator.
 *
 * Evaluates "3 4 + 5 *" -> 35.
 *
 * Pushes numbers onto a stack, applies operators on the top two when
 * we hit one. Single-digit operands only (keeps the lexer trivial).
 *
 * exit code = 35.
 */
#include <string.h>

#define CAP 32

typedef struct {
    int data[CAP];
    int size;
} Stack;

static void push(Stack *s, int v) {
    /* assume size < CAP */
    s->data[s->size++] = v;
}

static int pop(Stack *s) {
    /* assume size > 0 */
    return s->data[--s->size];
}

static int eval_rpn(const char *src) {
    Stack s = { .size = 0 };
    int i = 0;
    while (src[i]) {
        char c = src[i++];
        if (c == ' ') continue;
        if (c >= '0' && c <= '9') {
            push(&s, c - '0');
        } else {
            int b = pop(&s);
            int a = pop(&s);
            int r = 0;
            switch (c) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            case '*': r = a * b; break;
            case '/': r = a / b; break;
            }
            push(&s, r);
        }
    }
    return pop(&s);
}

int main(void) {
    return eval_rpn("3 4 + 5 *");   /* (3+4)*5 = 35 */
}
