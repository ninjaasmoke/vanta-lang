/* In-place quicksort (Lomuto partition).
 *
 * Sorts {3,1,4,1,5,9,2,6,5,3} ascending. exit code = element at index 5
 * after sorting = 4.
 *
 * sorted: 1 1 2 3 3 4 5 5 6 9
 *                   ^ index 5
 *
 * C does the work in 25 lines and gives you nothing for free. If the
 * partition is wrong, you'll find out when sort returns garbage.
 */

static void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }

static int partition(int *a, int lo, int hi) {
    int pivot = a[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        if (a[j] <= pivot) {
            i++;
            swap(&a[i], &a[j]);
        }
    }
    swap(&a[i + 1], &a[hi]);
    return i + 1;
}

static void qsort_(int *a, int lo, int hi) {
    if (lo < hi) {
        int p = partition(a, lo, hi);
        qsort_(a, lo, p - 1);
        qsort_(a, p + 1, hi);
    }
}

int main(void) {
    int a[10] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    qsort_(a, 0, 9);
    return a[5];   /* 4 */
}
