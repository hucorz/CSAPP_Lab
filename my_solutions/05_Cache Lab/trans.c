/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>

#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

void transpose_32x32(int A[32][32], int B[32][32]) {  // 8x8 当成一个块处理
    for (int i = 0; i < 32; i += 8) {
        for (int j = 0; j < 32; j += 8) {
            for (int a = 0; a < 8; a++) {
                int t0 = A[i + a][j];
                int t1 = A[i + a][j + 1];
                int t2 = A[i + a][j + 2];
                int t3 = A[i + a][j + 3];
                int t4 = A[i + a][j + 4];
                int t5 = A[i + a][j + 5];
                int t6 = A[i + a][j + 6];
                int t7 = A[i + a][j + 7];

                B[j][i + a] = t0;
                B[j + 1][i + a] = t1;
                B[j + 2][i + a] = t2;
                B[j + 3][i + a] = t3;
                B[j + 4][i + a] = t4;
                B[j + 5][i + a] = t5;
                B[j + 6][i + a] = t6;
                B[j + 7][i + a] = t7;
            }
        }
    }
}

void transpose_64x64(int A[64][64], int B[64][64]) {  // 8x8 当成一个块处理
    int t0, t1, t2, t3, t4, t5, t6, t7;
    int a;
    for (int i = 0; i < 64; i += 8) {
        for (int j = 0; j < 64; j += 8) {
            // 把 每个 8x8 分成 4 个 4x4 的块，标记为 1，2，3，4
            for (a = i; a < i + 4; a++) {
                // 把 A 中的 1，2 转置后的结果放到 B 中的 1，2
                t0 = A[a][j + 0];
                t1 = A[a][j + 1];
                t2 = A[a][j + 2];
                t3 = A[a][j + 3];
                t4 = A[a][j + 4];
                t5 = A[a][j + 5];
                t6 = A[a][j + 6];
                t7 = A[a][j + 7];

                B[j + 0][a] = t0;
                B[j + 1][a] = t1;
                B[j + 2][a] = t2;
                B[j + 3][a] = t3;

                B[j + 0][a + 4] = t4;
                B[j + 1][a + 4] = t5;
                B[j + 2][a + 4] = t6;
                B[j + 3][a + 4] = t7;
            }

            for (a = j; a < j + 4; a++) {
                // 获取 B 中的 2 备份
                t0 = B[a][i + 4];
                t1 = B[a][i + 5];
                t2 = B[a][i + 6];
                t3 = B[a][i + 7];
                // 将 A 中转置后的 3 移动到 B 中的 2
                t4 = A[i + 4][a];
                t5 = A[i + 5][a];
                t6 = A[i + 6][a];
                t7 = A[i + 7][a];

                B[a][i + 4] = t4;
                B[a][i + 5] = t5;
                B[a][i + 6] = t6;
                B[a][i + 7] = t7;
                // 将 备份的 B 的 2 移动到 B 的 3
                B[a + 4][i] = t0;
                B[a + 4][i + 1] = t1;
                B[a + 4][i + 2] = t2;
                B[a + 4][i + 3] = t3;
            }

            for (a = i + 4; a < i + 8; a++) {
                // 将 A 中转置后的 4 移动到 B 的 4
                t0 = A[a][j + 4];
                t1 = A[a][j + 5];
                t2 = A[a][j + 6];
                t3 = A[a][j + 7];

                B[j + 4][a] = t0;
                B[j + 5][a] = t1;
                B[j + 6][a] = t2;
                B[j + 7][a] = t3;
            }
        }
    }
}

void transpose_67x61(int A[67][61], int B[61][67]) {  // 16x16 就过了，神奇
    for (int i = 0; i < 67; i += 16) {
        for (int j = 0; j < 61; j += 16) {
            for (int a = i; a < i + 16 && a < 67; a++) {
                for (int b = j; b < j + 16 && b < 61; b++) {
                    B[b][a] = A[a][b];
                }
            }
        }
    }
}

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
    if (M == 32 && N == 32)
        transpose_32x32(A, B);
    else if (M == 64 && N == 64)
        transpose_64x64(A, B);
    else if (M == 61 && N == 67)
        transpose_67x61(A, B);
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N]) {
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions() {
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}
