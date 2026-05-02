/* 3x3 matrix multiply. exit code = result[2][2].
 *
 * A = {{1,2,3},{4,5,6},{7,8,9}}
 * B = identity
 * A*B = A, so result[2][2] = 9.
 *
 * In C, 2D is just a 2D stack array. Done.
 */

static int N = 3;

int main(void) {
    int A[3][3] = { {1,2,3}, {4,5,6}, {7,8,9} };
    int B[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    int C[3][3] = {0};

    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            for (int k = 0; k < N; k++)
                C[i][j] += A[i][k] * B[k][j];

    return C[2][2];   /* 9 */
}
