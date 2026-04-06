#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <stdlib.h>
#include <math.h>
#include <pthread.h> //for threads

typedef struct ARG{
	int **x;
	float *p;
	int q;
	int m;
	int n;
	int offset;
} args;

void *mse_ma(void *);

float* threaded_mse_ma(int **x, int q, int n, int t) {
	float *p = (float*)malloc(n * sizeof(float));
	pthread_t* tids = (pthread_t*)malloc(t * sizeof(pthread_t));
	args* arguments = (args*)malloc(t * sizeof(args));

	// Number of columns per thread submatrix
	int nt = n/t;


	for (int i = 0; i < t; i++) {
		int offset = i * nt;
		arguments[i].x = x;
		arguments[i].p = p;
		arguments[i].q = q;
		arguments[i].m = n;
		arguments[i].n = nt;
		arguments[i].offset = offset;

		// TODO: Distribute remainders
		// Catchbasin for uneven division
		if (i == t - 1) {
			arguments[i].n = n - (nt * i);
		}

		pthread_create(&tids[i], NULL, mse_ma, (void *) &arguments[i]);
	}

	for (int i = 0; i < t; i++) {
		pthread_join(tids[i], NULL);
	}

	return p;
}

void* mse_ma(void* argv) {
	args* params;
	params = (args*) argv;

	int **x = params->x;
	// Access float *p via params->p
	int q = params->q;
	int m = params->m;
	int n = params->n;
	int offset = params->offset;

	printf("===== Submatrix =====\n");
	 for (int i = 0; i < m; i++) {
		for (int j = offset; j < n + offset; j++)
			printf("%d\t", x[i][j]);
		printf("\n");
	 }

	for (int j = 1; j <= n; j++) {
		int actual_col = (j - 1) + offset;
		float sum = 0;
		float window_sum = 0;

		// Initialize sliding window
		for (int k = 0; k < q; k++) {
		    window_sum += x[k][actual_col];
		}

		for (int i = q + 1; i <= m; i++) {
		    float ma = window_sum / q;
		    sum += pow(x[i-1][actual_col] - ma, 2);

		// Prevents matrix out-of-bound
		    if (i < m) {
			// Sliding window main logic, subtract the previous, add the next
			window_sum += x[i-1][actual_col] - x[i - q - 1][actual_col];
		    }
		}

		// Add the computed p to the global p array
		params->p[j-1 + offset] = sqrt(sum) / (m - q);
    }
}

float max(float x, float y) {
	return x > y ? x : y;
}

float min(float x, float y) {
	return x < y ? x : y;
}

int main(int argc, char* argv[]) {
	// Initialize the random seed for RNG
	srand(time(NULL));

	int n, t, q, **matrix;

	// Arguments checker
	if (argc == 2) {
		char *filename = argv[1];
		FILE *file = fopen(filename, "r");
		
		if (file == NULL) {
			printf("Error: Could not open file %s\n", filename);
			return 1;
		}

		fscanf(file, "%d", &n);
		fscanf(file, "%d", &t); 
		fscanf(file, "%d", &q); 

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
		
		// Student number: 09848
		int ss = 98;
		q = (int) max(n/2, min(ss * n/100, 3*n/4));

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
		printf("Usage: ./lab02 <n> <t>, where n > 0.");
		return 1;
	}

	// Initialize p
	float *p;
	
	struct timespec start_time, end_time;
	clock_gettime(CLOCK_MONOTONIC, &start_time);

	p = threaded_mse_ma(matrix, q, n, t);

	clock_gettime(CLOCK_MONOTONIC, &end_time);

	printf("P: ");
	for (int i = 0; i < n; i++)
		printf("%.6f\t", p[i]);
	printf("\n");


	printf("Q: %d\n", q);
	printf("N: %d\n", n);
	double time_taken = (end_time.tv_sec - start_time.tv_sec) + 
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e9;	

	printf("Time Taken: %f sec\n", time_taken);
}


