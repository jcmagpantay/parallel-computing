#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

float* mse_ma(int** x, int q, int n) {
	float *p = (float*)malloc(n*sizeof(float));

	float sum = 0;
	// p_j loop
	for (int j = 1; j <= n; j++) {
		int current_sum = 0;
		int window_sum = 0;

		// Initial window sum
		for (int k = 1; k <= q; k++) {
			window_sum += x[k-1][j-1];
		}

		for (int i = q + 1; i <= n; i++) {		
			current_sum = x[i-1][j-1] - current_sum;

			// Square the current sum
			current_sum = current_sum * current_sum;
		}
		p[j-1] = sqrt(current_sum) / (n - q);
	}

	return p;
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

	// Arguments checker
	if (argc != 2) {
		printf("Usage: ./lab01 <n>, where n > 0 and n <= 100");
		return 1;
	}

	int n = atoi(argv[1]);
		
	// Student number: 09848
	int ss = 98;

	// Initialize q
	int q = (int) max(n/2, min(ss * n/100, 3*n/4));

	// Initialize p
	float *p;
	
	// Create nxn matrix
	int **matrix = (int**)malloc(n * sizeof(int*));

	for (int i = 0; i < n; i++)
		matrix[i] = (int*)malloc(n * sizeof(int));

	// Populate matrix with random numbers
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++)
			matrix[i][j] = (rand() % 100) + 1;
	
	clock_t start_time, end_time;

	start_time = clock();
	p = mse_ma(matrix, q, n);
	end_time = clock();

	printf("P: ");
	for (int i = 0; i < n; i++)
		printf("%.4f\t", p[i]);
	printf("\n");


	printf("Q: %d\n", q);
	printf("N: %d\n", n);
	double time_taken = ((double)(end_time - start_time))/CLOCKS_PER_SEC;
	printf("Time Taken: %f sec\n", time_taken);
}


