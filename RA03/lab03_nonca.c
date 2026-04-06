#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <windows.h>

typedef struct ARG {
    int **x;
    float *p;
    int q;
    int m;
    int n;
    int offset;
} args;

DWORD WINAPI mse_ma(LPVOID argv);

float* threaded_mse_ma(int **x, int q, int n, int t) {
    if (t <= 0) t = 1; // Prevent division by zero
    float *p = (float*)malloc(n * sizeof(float));
    HANDLE* tids = (HANDLE*)malloc(t * sizeof(HANDLE));
    args* arguments = (args*)malloc(t * sizeof(args));

    // Calculate base columns per thread and the remainder
    int base_nt = n / t;
    int remainder = n % t;

    for (int i = 0; i < t; i++) {
        // Distribute the remainder evenly across the first 'remainder' threads
        int current_nt = base_nt + (i < remainder ? 1 : 0);
        int offset = i * base_nt + (i < remainder ? i : remainder);
        
        arguments[i].x = x;
        arguments[i].p = p;
        arguments[i].q = q;
        arguments[i].m = n;
        arguments[i].n = current_nt; 
        arguments[i].offset = offset;

        // Create thread (no core affinity, OS scheduler decides placement)
        tids[i] = CreateThread(NULL, 0, mse_ma, &arguments[i], 0, NULL);
    }

    // Wait for all threads to complete
    for (int i = 0; i < t; i++) {
        if (tids[i] != NULL) {
            WaitForSingleObject(tids[i], INFINITE);
            CloseHandle(tids[i]);
        }
    }

    free(tids);
    free(arguments);

    return p;
}

DWORD WINAPI mse_ma(LPVOID argv) {
    args* params = (args*)argv;

    int **x = params->x;
    int q = params->q;
    int m = params->m;
    int n = params->n;
    int offset = params->offset;

    // Prevent division by zero
    if (m - q <= 0 || q <= 0) return 0;

    for (int j = 1; j <= n; j++) {
        int actual_col = (j - 1) + offset;
        float sum = 0.0f;
        float window_sum = 0.0f;

        // Initialize sliding window
        for (int k = 0; k < q; k++) {
            window_sum += x[k][actual_col];
        }

        for (int i = q + 1; i <= m; i++) {
            float ma = window_sum / q;
            // Pre-calculate difference before squaring for better performance
            float diff = x[i - 1][actual_col] - ma;
            sum += diff * diff;

            // Prevents matrix out-of-bound
            if (i < m) {
                // Sliding window main logic, subtract the previous, add the next
                window_sum += x[i - 1][actual_col] - x[i - q - 1][actual_col];
            }
        }

        // Add the computed p to the global p array
        params->p[actual_col] = sqrtf(sum) / (m - q);
    }
    
    return 0; // Return gracefully
}

int main(int argc, char* argv[]) {
    int n = 0, t = 0, q = 0, **matrix = NULL;

    // Arguments checker
    if (argc == 2) {
        char *filename = argv[1];
        FILE *file = fopen(filename, "r");
        
        if (file == NULL) {
            printf("Error: Could not open file %s\n", filename);
            return 1;
        }

        if (fscanf(file, "%d", &n) != 1 || fscanf(file, "%d", &t) != 1 || fscanf(file, "%d", &q) != 1) {
            printf("Error: Invalid file format\n");
            fclose(file);
            return 1;
        }

        if (n <= 0 || t <= 0) {
            printf("Error: n and t must be positive integers.\n");
            fclose(file);
            return 1;
        }

        matrix = (int**)malloc(n * sizeof(int*));
        for (int i = 0; i < n; i++) {
            matrix[i] = (int*)malloc(n * sizeof(int));
            for (int j = 0; j < n; j++) {
                if (j == n - 1) {
                    fscanf(file, "%d", &matrix[i][j]);
                } else {
                    fscanf(file, "%d,", &matrix[i][j]); 
                }
            }
        }
        fclose(file);
    }
    else if (argc == 3) {
        n = atoi(argv[1]);
        t = atoi(argv[2]);
        
        if (n <= 0 || t <= 0) {
            printf("Error: n and t must be positive integers.\n");
            return 1;
        }

        // Student number: 09848
        int ss = 98;
        // Utilize the max/min macros from conditional `<windows.h>` includes
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
    }
    else {
        printf("Usage: %s <filename> OR %s <n> <t>, where n > 0.\n", argv[0], argv[0]);
        return 1;
    }

    float *p;
    LARGE_INTEGER frequency, start_time, end_time;

    // Use Windows QueryPerformanceCounter for high-resolution timing
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start_time);

    p = threaded_mse_ma(matrix, q, n, t);

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
