/* Singly linked list: build {1,2,3,4,5}, then sum it.
 *
 * exit code = sum = 15.
 *
 * C handles linked structures naturally - declare a Node, point to
 * the next one, malloc/free as you go. The whole thing is 30 lines.
 */
#include <stdlib.h>

typedef struct Node {
    int value;
    struct Node *next;
} Node;

static Node *cons(int v, Node *n) {
    Node *x = (Node *)malloc(sizeof(Node));
    x->value = v;
    x->next  = n;
    return x;
}

static int list_sum(Node *n) {
    int s = 0;
    while (n) { s += n->value; n = n->next; }
    return s;
}

static void list_free(Node *n) {
    while (n) { Node *next = n->next; free(n); n = next; }
}

int main(void) {
    Node *l = NULL;
    for (int i = 5; i >= 1; i--) l = cons(i, l);
    int s = list_sum(l);
    list_free(l);
    return s;   /* 15 */
}
