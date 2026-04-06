#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <windows.h>

float* mse_ma(int **x, int q, int n) {
    float *p = (float*)malloc(n * sizeof(float));

    // Prevent division by zero
    if (n - q <= 0 || q <= 0) return p;

    for (int j = 1; j <= n; j++) {
        float sum = 0.0f;
        float window_sum = 0.0f;

        // Initialize sliding window
        for (int k = 0; k < q; k++) {
            window_sum += x[k][j - 1];
        }

        for (int i = q + 1; i <= n; i++) {
            float ma = window_sum / q;
            // Pre-calculate difference before squaring for better performance
            float diff = x[i - 1][j - 1] - ma;
            sum += diff * diff;

            // Prevents matrix out-of-bound
            if (i < n) {
                // Sliding window main logic, subtract the previous, add the next
                window_sum += x[i - 1][j - 1] - x[i - q - 1][j - 1];
            }
        }

        // Add the computed p to the global p array
        p[j - 1] = sqrtf(sum) / (n - q);
    }

    return p;
}

int main(int argc, char* argv[]) {
    int n = 0, q = 0, **matrix = NULL;

    // Arguments checker
    if (argc != 2) {
        printf("Usage: %s <n>, where n > 0.\n", argv[0]);
        return 1;
    }

    n = atoi(argv[1]);

    if (n <= 0) {
        printf("Error: n must be a positive integer.\n");
        return 1;
    }

    // Student number: 09848
    int ss = 98;
    q = max(n / 2, min(ss * n / 100, 3 * n / 4));

    // Initialize the random seed for RNG
    srand((unsigned int)time(NULL));

    // Allocate and populate the random matrix [cite: 21]
    matrix = (int**)malloc(n * sizeof(int*));
    for (int i = 0; i < n; i++) {
        matrix[i] = (int*)malloc(n * sizeof(int));
        for (int j = 0; j < n; j++) {
            matrix[i][j] = (rand() % 100) + 1;
        }
    }

    float *p;
    LARGE_INTEGER frequency, start_time, end_time;

    // Use Windows QueryPerformanceCounter for high-resolution timing
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start_time);

    p = mse_ma(matrix, q, n);

    QueryPerformanceCounter(&end_time);

    printf("P: ");
    for (int i = 0; i < n; i++) {
        printf("%.6f\t", p[i]);
    }
    printf("\n");

    printf("Q: %d\n", q);
    printf("N: %d\n", n);
    
    double time_taken = (double)(end_time.QuadPart - start_time.QuadPart) / (double)frequency.QuadPart;
    printf("Time Taken: %f sec\n", time_taken);

    // Free all allocated memory effectively
    free(p);
    for (int i = 0; i < n; i++) {
        free(matrix[i]);
    }
    free(matrix);

    return 0;
}
