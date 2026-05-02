/* Binary search on a sorted array.
 *
 * Notice the manual `assert(n >= 0)` we'd add in real code.
 * Notice also the classic `(low + high) / 2` overflow trap that we
 * have to remember to write as `low + (high - low) / 2`. The
 * compiler does not help us here at all — the precondition lives
 * in a comment, the postcondition lives in our heads.
 *
 * exit code = index of 7 in {1,3,5,7,9,11} = 3.
 */
#include <assert.h>

/* preconditions:  n >= 0,  a is sorted ascending
 * postconditions: -1, or 0 <= result < n with a[result] == target
 *
 * --- the comment above is the full contract. nothing enforces it. ---
 */
static int bsearch_(const int *a, int n, int target) {
    assert(n >= 0);                              /* hand-rolled */
    int low = 0;
    int high = n;
    while (low < high) {
        int mid = low + (high - low) / 2;        /* avoid overflow */
        if (a[mid] == target) return mid;
        if (a[mid] < target) low = mid + 1;
        else                 high = mid;
    }
    return -1;
}

int main(void) {
    int a[] = {1, 3, 5, 7, 9, 11};
    return bsearch_(a, 6, 7);   /* 3 */
}
