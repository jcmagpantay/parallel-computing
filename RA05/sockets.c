#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <math.h>

#define MAX_NODES 256


// helper struct to see where each node's ip is and their ports...
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int  port;
} NodeEntry;

// Wrapper utility function to make sure all bytes are sent.
int send_all(int sock, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;
    while (total_sent < len) {
        ssize_t sent = send(sock, ptr + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            perror("send_all failed");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

// Wrapper utility function to make sure all EXPECTED bytes are received.
int recv_all(int sock, void *buf, size_t len) {
    size_t total_recv = 0;
    char *ptr = (char *)buf;
    while (total_recv < len) {
        ssize_t received = recv(sock, ptr + total_recv, len - total_recv, 0);
        if (received <= 0) {
            perror("recv_all failed");
            return -1;
        }
        total_recv += received;
    }
    return 0;
}


// Setup a listening socket on the given port
int setup_server(int port) {
    int listening_socket;
    struct sockaddr_in address;
    int opt = 1;

    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }

// this just fixes the port alreeady in use problem.
#ifdef SO_REUSEPORT
    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEPORT failed");
    }
#endif

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Retry bind for up to 15s in case port is in TIME_WAIT from a previous run
    int bind_retries = 30;
    while (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (--bind_retries == 0) {
            perror("Bind failed (port stuck in TIME_WAIT?)");
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "[Port %d] Bind failed, retrying in 0.5s...\n", port);
        usleep(500000);
    }

    if (listen(listening_socket, 32) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return listening_socket;
}


// helper function to connect to SPECIFIC IP Address!!!
// used by each single node to identify the node to connect to.
// this kinda essential with da tree method cuz every single node becomes both a server and a client.
int connect_to(const char *ip, int port) {
    int sock_fd;
    struct sockaddr_in serv_addr;

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address: %s\n", ip);
        close(sock_fd);
        return -1;
    }

    // allow for retries in case of unsynchronized setups
    // this makes for remote ssh to be easier without too many errors.
    int retries = 60;  // 60 × 0.5s = 30s max wait (needed for slow Linux terminal spawning)
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        retries--;
        if (retries == 0) {
            printf("Connection to %s:%d failed after retries.\n", ip, port);
            close(sock_fd);
            return -1;
        }
        // sleep for 0.5 seconds before trying to connect again
        usleep(500000);
    }

    return sock_fd;
}

// HELPER FUNCTION: read config for LOCAL mode
// Format of config must be: (config.local.txt)
// <ip> <port>
int read_config_local(const char *filename, NodeEntry nodes[], int total_nodes) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("Cannot open config.local.txt"); return -1; }

    char ip[INET_ADDRSTRLEN];
    int base_port;
    if (fscanf(f, "%s %d", ip, &base_port) != 2) {
        printf("Error: Invalid config.local.txt format. Expected: <ip> <base_port>\n");
        fclose(f);
        return -1;
    }
    fclose(f);

    for (int i = 0; i < total_nodes; i++) {
        strncpy(nodes[i].ip, ip, INET_ADDRSTRLEN);
        nodes[i].port = base_port + i;
    }
    return total_nodes;
}

// HELPER FUNCTION: read config for REMOTE mode
// Format of config must be: (config.remote.txt)
// <ip> <port>
// the first line is the master
int read_config_remote(const char *filename, NodeEntry nodes[], int max_nodes) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("Cannot open config.remote.txt"); return -1; }

    int count = 0;
    while (count < max_nodes &&
           fscanf(f, "%s %d", nodes[count].ip, &nodes[count].port) == 2) {
        count++;
    }
    fclose(f);
    return count;
}

// helper to create random matrix
int** create_matrix(int n) {
    int **M = (int **)malloc(n * sizeof(int *));
    for (int i = 0; i < n; i++) {
        M[i] = (int *)malloc(n * sizeof(int));
        for (int j = 0; j < n; j++) {
            M[i][j] = (rand() % 100) + 1;
        }
    }
    return M;
}

void print_matrix(int **M, int rows, int cols) {
    // Only print if matrix is reasonably small (pretty printing xD)
    if (rows > 20 || cols > 20) {
        printf("  [%d x %d matrix — too large to display]\n", rows, cols);
        return;
    }
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%4d", M[i][j]);
            if (j < cols - 1) printf(" ");
        }
        printf("\n");
    }
}

void free_matrix(int **M, int rows) {
    if (!M) return;
    for (int i = 0; i < rows; i++) free(M[i]);
    free(M);
}

// Compute Q from student number (SS=98) and matrix size, same as RA01
int compute_q(int n) {
    int ss = 98;
    float fmax = (float)n/2;
    float fmin = (float)ss * n / 100.0f;
    float cap  = 3.0f * n / 4.0f;
    if (fmin > cap) fmin = cap;
    if (fmax > fmin) return (int)fmax;
    return (int)fmin;
}

// Transpose an n×n matrix: X^T[j][i] = X[i][j]
// After transpose, each row of X^T contains all data for one column of X.
int** transpose_matrix(int **M, int n) {
    int **T = (int **)malloc(n * sizeof(int *));
    for (int j = 0; j < n; j++) {
        T[j] = (int *)malloc(n * sizeof(int));
        for (int i = 0; i < n; i++) {
            T[j][i] = M[i][j];
        }
    }
    return T;
}

// Compute Order Q MA MSE on transposed submatrix.
// Each row of the submatrix is a FULL original column (length = col_len).
// Returns a float vector of length `local_rows` (one p[j] per assigned column).
float* compute_ma(int **submatrix, int local_rows, int col_len, int q) {
    float *p = (float *)calloc(local_rows, sizeof(float));
    if (q >= col_len) q = col_len - 1;
    if (q <= 0) return p;

    for (int r = 0; r < local_rows; r++) {
        int *col = submatrix[r];

        // Prime the sliding window with the first q elements
        float window_sum = 0;
        for (int k = 0; k < q; k++)
            window_sum += col[k];

        float sum_sq = 0;
        for (int i = q; i < col_len; i++) {
            // ma = average of col[i-q .. i-1], maintained as sliding sum
            float ma = window_sum / q;
            float diff = col[i] - ma;
            sum_sq += diff * diff;

            // Slide: add col[i], remove col[i-q]
            window_sum += col[i] - col[i - q];
        }
        p[r] = sqrtf(sum_sq) / (col_len - q);
    }
    return p;
}

// Send a float vector over socket (network-safe)
void send_vector(int sock, float *vec, int len) {
    uint32_t nl = htonl((uint32_t)len);
    send_all(sock, &nl, sizeof(nl));
    // Send floats as raw bytes (same architecture assumed, or use fixed encoding)
    send_all(sock, vec, len * sizeof(float));
}

// Receive a float vector over socket
float* recv_vector(int sock, int *out_len) {
    uint32_t nl;
    recv_all(sock, &nl, sizeof(nl));
    int len = (int)ntohl(nl);
    float *vec = (float *)malloc(len * sizeof(float));
    recv_all(sock, vec, len * sizeof(float));
    *out_len = len;
    return vec;
}

void print_vector(float *vec, int len) {
    if (len > 20) {
        printf("  [vector of %d elements — too large to display]\n", len);
        return;
    }
    printf("  ");
    for (int i = 0; i < len; i++) {
        printf("%.4f ", vec[i]);
    }
    printf("\n");
}

// Master (node 0) gets 0 rows — only slaves compute.
// Rows are distributed evenly among slaves (nodes 1..total_nodes-1).
int rows_for_node(int node_id, int n, int total_nodes) {
    if (node_id == 0) return 0;
    int num_slaves = total_nodes - 1;
    int slave_idx = node_id - 1;
    return (n / num_slaves) + (slave_idx < (n % num_slaves) ? 1 : 0);
}

int rows_for_subtree(int start_node, int subtree_size, int n, int total_nodes) {
    int total = 0;
    for (int i = start_node; i < start_node + subtree_size && i < total_nodes; i++) {
        total += rows_for_node(i, n, total_nodes);
    }
    return total;
}

int start_row_for_node(int node_id, int n, int total_nodes) {
    int start = 0;
    for (int i = 0; i < node_id; i++) {
        start += rows_for_node(i, n, total_nodes);
    }
    return start;
}

// helper function to send matrices over socket.
// sends first row and cols then sends by row
void send_submatrix(int sock, int **data, int start_row, int num_rows, int cols) {
    // send first how many row and col the receiver will get
    // network safe encoding
    uint32_t nr = htonl((uint32_t)num_rows);
    uint32_t nc = htonl((uint32_t)cols);

    send_all(sock, &nr, sizeof(nr));
    send_all(sock, &nc, sizeof(nc));

    for (int r = 0; r < num_rows; r++) {
        // send matrix per row not all of it.
        // convert to 
        uint32_t *buf = (uint32_t *)malloc(cols * sizeof(uint32_t));
        for (int c = 0; c < cols; c++) {
            buf[c] = htonl((uint32_t)data[start_row + r][c]);
        }
        send_all(sock, buf, cols * sizeof(uint32_t));
        free(buf);
    }
}

// this is mirrored with send submatrix
// receives row and column first
// then receives per row
int** recv_submatrix(int sock, int *out_rows, int *out_cols) {
    uint32_t nr, nc;
    recv_all(sock, &nr, sizeof(nr));
    recv_all(sock, &nc, sizeof(nc));
    int rows = (int)ntohl(nr);
    int cols = (int)ntohl(nc);

    int **data = (int **)malloc(rows * sizeof(int *));
    for (int r = 0; r < rows; r++) {
        data[r] = (int *)malloc(cols * sizeof(int));
        uint32_t *buf = (uint32_t *)malloc(cols * sizeof(uint32_t));
        recv_all(sock, buf, cols * sizeof(uint32_t));
        for (int c = 0; c < cols; c++) {
            data[r][c] = (int)ntohl(buf[c]);
        }
        free(buf);
    }

    *out_rows = rows;
    *out_cols = cols;
    return data;
}

// LINEAR BROADCASTING STRATEGY!!! O(n)
// Not used in the research paper.
void scatter_linear(int my_id, int total_nodes, int n,
                    int **full_matrix, int listening_socket,
                    NodeEntry nodes[],
                    int ***out_data, int *out_rows, int *out_cols,
                    struct timespec *time_before, int *time_started) {
    int cols = n;

    // Master node
    if (my_id == 0) {
        // Used ai for pretty printing xD
        printf("┌─────────────────────────────────────────┐\n");
        printf("│  LINEAR SCATTER: Sending to %d slaves    │\n", total_nodes - 1);
        printf("└─────────────────────────────────────────┘\n\n");

        for (int target = 1; target < total_nodes; target++) {
            int t_rows = rows_for_node(target, n, total_nodes);
            int t_start = start_row_for_node(target, n, total_nodes);

            printf("──── Step %d/%d ────────────────────────────\n", target, total_nodes - 1);
            printf("  Node 0 ──→ Node %d\n", target);
            printf("  Sending rows [%d..%d] (%d rows x %d cols)\n",
                   t_start, t_start + t_rows - 1, t_rows, cols);
            print_matrix(&full_matrix[t_start], t_rows, cols);
            printf("\n");

            int sock = connect_to(nodes[target].ip, nodes[target].port);
            if (sock < 0) {
                printf("  ✗ Failed to connect to Node %d!\n", target);
                continue;
            }

            send_submatrix(sock, full_matrix, t_start, t_rows, cols);
            close(sock);
            printf("  ✓ Sent to Node %d at %s:%d\n\n", target, nodes[target].ip, nodes[target].port);
        }

        // master node has its own rows
        int my_r = rows_for_node(0, n, total_nodes);
        *out_data = (int **)malloc(my_r * sizeof(int *));
        for (int r = 0; r < my_r; r++) {
            (*out_data)[r] = (int *)malloc(cols * sizeof(int));
            memcpy((*out_data)[r], full_matrix[r], cols * sizeof(int));
        }
        *out_rows = my_r;
        *out_cols = cols;

        printf("──── Master's portion ──────────────────────\n");
        printf("  Keeping rows [0..%d] (%d rows)\n", my_r - 1, my_r);
        print_matrix(*out_data, *out_rows, cols);
        printf("\n");

    } else {
        // this is a slave node.
        printf("  Waiting for data from Master (Node 0)...\n\n");

        struct sockaddr_in addr;
        int addrlen = sizeof(addr);
        int conn = accept(listening_socket, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
        if (conn < 0) { perror("Accept failed"); return; }

        if (!*time_started) {
            clock_gettime(CLOCK_MONOTONIC, time_before);
            *time_started = 1;
        }

        *out_data = recv_submatrix(conn, out_rows, out_cols);
        close(conn);

        printf("  ✓ Received %d rows x %d cols from Node 0\n", *out_rows, *out_cols);
        printf("\n  Received submatrix:\n");
        print_matrix(*out_data, *out_rows, *out_cols);
        printf("\n");
    }
}

// O(log n) one-to-many personalized broadcast!!
// Logic: binomial tree
// Uses stride that gradually decreases
// from n/2 to 1
void scatter_tree(int my_id, int total_nodes, int n,
                  int **full_matrix, int listening_socket,
                  NodeEntry nodes[],
                  int ***out_data, int *out_rows, int *out_cols,
                  struct timespec *time_before, int *time_started) {
    int cols = n;
    int has_data = 0;
    int my_rows = 0;
    int **my_data = NULL;

    // if master
    if (my_id == 0) {
        my_data = full_matrix;
        my_rows = n;
        has_data = 1;
    }

    // deals with cases that are not exactly matching with power of 2
    // in the case of not exact, it rounds up to the next power of 2
    int hp2 = 1;
    while (hp2 < total_nodes) hp2 *= 2;
    int round = 0;
    int total_rounds = 0;

    // this is a manual logatrithm loop lol log2(hp2)
    { int tmp = hp2; while (tmp > 1) { total_rounds++; tmp /= 2; } }

    // pretty print by ai
    printf("┌─────────────────────────────────────────┐\n");
    printf("   TREE SCATTER: %d rounds (log₂%d)         \n", total_rounds, total_nodes);
    printf("└─────────────────────────────────────────┘\n\n");

    for (int stride = hp2 / 2; stride > 0; stride /= 2) {
        round++;

        // node already has the data, needs to send.
        if (has_data) {
            int target = my_id + stride;
            if (target < total_nodes) {
                int target_subtree_size = stride;
                if (target + stride > total_nodes)
                    target_subtree_size = total_nodes - target;
                int send_rows = rows_for_subtree(target, target_subtree_size, n, total_nodes);
                int keep_rows = my_rows - send_rows;

                printf("──── Round %d/%d (stride=%d) ────────────────\n", round, total_rounds, stride);
                printf("  Node %d ──→ Node %d\n", my_id, target);
                printf("  Splitting: send %d rows, keep %d rows\n", send_rows, keep_rows);
                if (cols <= 20) {
                    printf("\n  Submatrix being sent to Node %d:\n", target);
                    print_matrix(&my_data[keep_rows], send_rows, cols);
                }

                int sock = connect_to(nodes[target].ip, nodes[target].port);
                if (sock < 0) {
                    printf("  ✗ Failed to connect to Node %d!\n", target);
                    return;
                }
                send_submatrix(sock, my_data, keep_rows, send_rows, cols);
                close(sock);

                // Free sent rows, shrink
                for (int r = keep_rows; r < my_rows; r++) {
                    free(my_data[r]);
                }
                my_rows = keep_rows;
                if (my_rows > 0) {
                    my_data = (int **)realloc(my_data, my_rows * sizeof(int *));
                } else {
                    free(my_data);
                    my_data = NULL;
                }

                printf("\n  ✓ Sent! Now holding %d rows\n\n", my_rows);
            }
        // node is waiting to receive data.
        } else {
            // checks if it is their turn in the local group per stride
            if ((my_id % (stride * 2)) == stride) {
                printf("──── Round %d/%d (stride=%d) ────────────────\n", round, total_rounds, stride);
                printf("  Node %d: Waiting to receive...\n", my_id);

                struct sockaddr_in addr;
                int addrlen = sizeof(addr);
                int conn = accept(listening_socket, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
                if (conn < 0) { perror("Accept failed"); return; }

                if (!*time_started) {
                    clock_gettime(CLOCK_MONOTONIC, time_before);
                    *time_started = 1;
                }

                my_data = recv_submatrix(conn, &my_rows, &cols);
                close(conn);
                has_data = 1;

                int parent = my_id - stride;
                printf("  ✓ Received %d rows x %d cols from Node %d\n", my_rows, cols, parent >= 0 ? parent : 0);
                if (cols <= 20) {
                    printf("\n  Received submatrix:\n");
                    print_matrix(my_data, my_rows, cols);
                }
                printf("\n");
            }
        }
    }

    // THis portion of code is to avoid double free-ing the matrices
    if (my_id == 0) {
        // Master ends with 0 rows (all distributed to slaves)
        if (my_rows > 0) {
            int **copy = (int **)malloc(my_rows * sizeof(int *));
            for (int r = 0; r < my_rows; r++) {
                copy[r] = (int *)malloc(cols * sizeof(int));
                memcpy(copy[r], my_data[r], cols * sizeof(int));
            }
            *out_data = copy;
        } else {
            *out_data = NULL;
        }
    } else {
        *out_data = my_data;
    }
    *out_rows = my_rows;
    *out_cols = cols;
}

// M1PR: Many-to-One Personalized Reduction via reverse binomial tree
// Each node has a partial vector p. The tree reverses the scatter:
// stride goes 1 → 2 → 4 → ... (opposite of scatter)
// Nodes that received in scatter now SEND back, and vice versa.
// Parent concatenates child's vector onto its own.
void gather_tree(int my_id, int total_nodes,
                 float **my_p, int *my_p_len,
                 int listening_socket, NodeEntry nodes[]) {
    int hp2 = 1;
    while (hp2 < total_nodes) hp2 *= 2;
    int total_rounds = 0;
    { int tmp = hp2; while (tmp > 1) { total_rounds++; tmp /= 2; } }

    int round = 0;

    printf("┌─────────────────────────────────────────┐\n");
    printf("   M1PR GATHER: %d rounds (log₂%d)          \n", total_rounds, total_nodes);
    printf("└─────────────────────────────────────────┘\n\n");

    // Reverse of scatter: stride goes 1, 2, 4, ...
    for (int stride = 1; stride < hp2; stride *= 2) {
        round++;

        // In scatter, node (my_id + stride) received from my_id.
        // In gather, node (my_id + stride) SENDS to my_id.
        int child = my_id + stride;
        int parent = my_id - stride;

        // Am I a receiver (parent) this round?
        if (child < total_nodes && (my_id % (stride * 2)) == 0) {
            printf("──── Round %d/%d (stride=%d) ────────────────\n", round, total_rounds, stride);
            printf("  Node %d: Waiting for vector from Node %d...\n", my_id, child);

            struct sockaddr_in addr;
            int addrlen = sizeof(addr);
            int conn = accept(listening_socket, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
            if (conn < 0) { perror("Accept failed in gather"); return; }

            int child_len = 0;
            float *child_p = recv_vector(conn, &child_len);
            close(conn);

            // Concatenate: my_p = [my_p | child_p]
            int new_len = *my_p_len + child_len;
            if (*my_p == NULL) {
                // First receive — just take child's data
                *my_p = child_p;
                child_p = NULL;  // prevent free below
            } else {
                *my_p = (float *)realloc(*my_p, new_len * sizeof(float));
                memcpy((*my_p) + *my_p_len, child_p, child_len * sizeof(float));
            }
            *my_p_len = new_len;
            if (child_p) free(child_p);

            printf("  ✓ Received %d elements from Node %d, now holding %d elements\n\n",
                   child_len, child, new_len);
        }

        // Am I a sender (child) this round?
        if (parent >= 0 && (my_id % (stride * 2)) == stride) {
            printf("──── Round %d/%d (stride=%d) ────────────────\n", round, total_rounds, stride);
            printf("  Node %d ──→ Node %d (sending %d elements)\n", my_id, parent, *my_p_len);

            int sock = connect_to(nodes[parent].ip, nodes[parent].port);
            if (sock < 0) {
                printf("  ✗ Failed to connect to Node %d!\n", parent);
                return;
            }
            send_vector(sock, *my_p, *my_p_len);
            close(sock);
            printf("  ✓ Sent!\n\n");
        }
    }
}

// CORE AFFINE code.
// i used ai here to help me make this code functional in both mac and linux
void pin_to_core(int core_id) {
#ifdef __linux__
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores > 0) {
        int target_core = core_id % num_cores;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(target_core, &cpuset);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0) {
            printf("[Binding] Process firmly bound to logical CPU Core %d\n", target_core);
        } else {
            perror("[Binding] sched_setaffinity failed");
        }
    }
#else
    (void)core_id; // prevent unused warning
#endif
}

// ============================================================
// STANDALONE TEST: ./lab05 test
// Known 5x5 matrix X[i][j] = i*j (1-indexed), q=3
// Expected p: [1.414214, 2.828427, 4.242641, 5.656854, 7.071068]
// ============================================================
int run_test() {
    printf("╔════════════════════════════════════════════╗\n");
    printf("║  STANDALONE MA TEST (no networking)        ║\n");
    printf("╠════════════════════════════════════════════╣\n");
    printf("║  Matrix: 5x5  X[i][j] = i*j  (1-indexed)  ║\n");
    printf("║  Q = 3                                     ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");

    int n = 5, q = 3;
    int **M = (int **)malloc(n * sizeof(int *));
    for (int i = 0; i < n; i++) {
        M[i] = (int *)malloc(n * sizeof(int));
        for (int j = 0; j < n; j++) {
            M[i][j] = (i + 1) * (j + 1);  // 1-indexed: X[i][j] = i*j
        }
    }

    printf("Matrix X:\n");
    print_matrix(M, n, n);
    printf("\n");

    // Transpose: each row of T is a full column of X
    int **T = transpose_matrix(M, n);
    printf("Transposed X^T:\n");
    print_matrix(T, n, n);
    printf("\n");

    // compute_ma on transposed: local_rows=n (all columns), col_len=n (all original rows)
    float *p = compute_ma(T, n, n, q);

    float expected[] = {1.414214f, 2.828427f, 4.242641f, 5.656854f, 7.071068f};

    printf("Computed p:  ");
    for (int i = 0; i < n; i++) printf("%.6f  ", p[i]);
    printf("\n");

    printf("Expected p:  ");
    for (int i = 0; i < n; i++) printf("%.6f  ", expected[i]);
    printf("\n\n");

    int pass = 1;
    for (int i = 0; i < n; i++) {
        float diff = fabsf(p[i] - expected[i]);
        if (diff > 0.001f) {
            printf("  ✗ p[%d]: got %.6f, expected %.6f (diff=%.6f)\n", i, p[i], expected[i], diff);
            pass = 0;
        }
    }

    if (pass) {
        printf("  ✓ ALL TESTS PASSED\n\n");
    } else {
        printf("\n  ✗ SOME TESTS FAILED\n\n");
    }

    free(p);
    free_matrix(T, n);
    free_matrix(M, n);
    return pass ? 0 : 1;
}

// MAIN
int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    // Quick test mode: ./lab05 test
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        return run_test();
    }

    // Usage: ./lab05 <n> <node_id> <mode> <total_nodes> <strategy> [affine]
    if (argc < 6) {
        printf("Usage: %s <n> <node_id> <mode> <total_nodes> <strategy> [affine]\n\n", argv[0]);
        printf("  n           = size of the square matrix (n x n)\n");
        printf("  node_id     = node identifier (0 = master, 1+ = slave)\n");
        printf("  mode        = 'local' or 'remote'\n");
        printf("  total_nodes = total number of participating nodes\n");
        printf("  strategy    = 'linear' (O(n)) or 'tree' (O(log n))\n");
        printf("  affine      = 'affine' to pin nodes to cores (Linux only, optional)\n\n");
        printf("Examples (4 nodes, 12x12 matrix):\n");
        printf("  Master:  %s 12 0 local 4 tree\n", argv[0]);
        printf("  Slave 1: %s 12 1 local 4 tree\n", argv[0]);
        printf("  Slave 2: %s 12 2 local 4 tree\n", argv[0]);
        printf("  Slave 3: %s 12 3 local 4 tree\n", argv[0]);
        return -1;
    }

    int n           = atoi(argv[1]);
    int my_id       = atoi(argv[2]);
    char *mode      = argv[3];
    int total_nodes = atoi(argv[4]);
    char *strategy  = argv[5];

    // check if arguments are correct
    if (n <= 0) {
        printf("Error: n must be a positive integer.\n");
        return -1;
    }
    if (total_nodes <= 0 || my_id < 0 || my_id >= total_nodes) {
        printf("Error: node_id must be in [0, total_nodes).\n");
        return -1;
    }
    if (strcmp(mode, "local") != 0 && strcmp(mode, "remote") != 0) {
        printf("Error: mode must be 'local' or 'remote'.\n");
        return -1;
    }
    if (strcmp(strategy, "linear") != 0 && strcmp(strategy, "tree") != 0) {
        printf("Error: strategy must be 'linear' or 'tree'.\n");
        return -1;
    }

    int use_tree = (strcmp(strategy, "tree") == 0);
    int use_affine = 0;
    int test_mode = 0;

    // Check remaining args for 'affine' and 'test' flags
    for (int a = 6; a < argc; a++) {
        if (strcmp(argv[a], "affine") == 0) use_affine = 1;
        if (strcmp(argv[a], "test") == 0)   test_mode = 1;
    }

    // test mode forces n=5
    if (test_mode) n = 5;

    // apply the core affine code.
    if (use_affine && strcmp(mode, "local") == 0) {
        pin_to_core(my_id);
    }

    // Read config to learn all node addresses
    NodeEntry nodes[MAX_NODES];
    int num_nodes;

    if (strcmp(mode, "local") == 0) {
        num_nodes = read_config_local("config.local.txt", nodes, total_nodes);
    } else {
        num_nodes = read_config_remote("config.generated.txt", nodes, MAX_NODES);

        // Debugging if SSH works
        if (my_id != 0) {
            const char *ssh_conn = getenv("SSH_CONNECTION");
            const char *ssh_client = getenv("SSH_CLIENT");
            if (ssh_conn) {
                printf("*** SSH PROOF: SSH_CONNECTION = %s\n", ssh_conn);
                printf("*** SSH PROOF: SSH_CLIENT     = %s\n",
                       ssh_client ? ssh_client : "(not set)");
                printf("\n");
            }
        }
    }

    if (num_nodes < 0) return -1;

    int my_port = nodes[my_id].port;

    // banner print WHAHAHA THANK U AI!
    printf("╔════════════════════════════════════════════╗\n");
    if (my_id == 0)
        printf("║  MASTER NODE (Node 0)                      ║\n");
    else
        printf("║  SLAVE NODE  (Node %-2d)                     ║\n", my_id);
    printf("╠════════════════════════════════════════════╣\n");
    printf("║  Matrix:   %5d x %-5d                   ║\n", n, n);
    printf("║  Port:     %-5d                           ║\n", my_port);
    printf("║  Mode:     %-8s                        ║\n", mode);
    printf("║  Nodes:    %-3d                             ║\n", total_nodes);
    printf("║  Strategy: %-8s %s     ║\n",
           use_tree ? "TREE" : "LINEAR",
           use_tree ? "O(log n)" : "O(n)    ");
    printf("╚════════════════════════════════════════════╝\n\n");

    // Setup listening socket
    int listening_socket = setup_server(my_port);
    printf("[Node %d] Listening on port %d\n\n", my_id, my_port);

    // Master creates the full matrix, then transposes for column-based distribution
    int **full_matrix = NULL;

    if (my_id == 0) {
        int **original;
        if (test_mode) {
            // Known test matrix: X[i][j] = (i+1)*(j+1), n=5, expected q=3
            printf("[Node 0] TEST MODE — using known 5x5 matrix\n\n");
            original = (int **)malloc(n * sizeof(int *));
            for (int i = 0; i < n; i++) {
                original[i] = (int *)malloc(n * sizeof(int));
                for (int j = 0; j < n; j++)
                    original[i][j] = (i + 1) * (j + 1);
            }
        } else {
            srand((unsigned int)time(NULL));
            original = create_matrix(n);
        }

        printf("[Node 0] Generated %d x %d matrix X%s\n", n, n,
               n <= 20 ? ":" : " (too large to print)");
        if (n <= 20) { print_matrix(original, n, n); printf("\n"); }

        // Transpose in-place (swap X[i][j] <-> X[j][i])
        // Avoids allocating a second n×n block — halves peak memory.
        printf("[Node 0] Transposing in-place...\n");
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                int tmp = original[i][j];
                original[i][j] = original[j][i];
                original[j][i] = tmp;
            }
        }
        full_matrix = original;  // reuse same allocation

        if (n <= 20) {
            printf("[Node 0] Transposed X^T:\n");
            print_matrix(full_matrix, n, n);
            printf("\n");
        }
    }

    // Check the time (master only — slaves time differently)
    struct timespec time_before;
    int time_started = 0;

    if (my_id == 0) {
        // Run the time when master is launched
        clock_gettime(CLOCK_MONOTONIC, &time_before);
        time_started = 1;
    }

    // Run the scatter strategy (linear / tree)
    int **my_data = NULL;
    int my_rows = 0, my_cols = 0;

    if (use_tree) {
        scatter_tree(my_id, total_nodes, n, full_matrix, listening_socket,
                     nodes, &my_data, &my_rows, &my_cols,
                     &time_before, &time_started);
    } else {
        scatter_linear(my_id, total_nodes, n, full_matrix, listening_socket,
                       nodes, &my_data, &my_rows, &my_cols,
                       &time_before, &time_started);
    }

    // After scatter, print final submatrix
    if (my_rows > 0) {
        printf("╔════════════════════════════════════════════╗\n");
        printf("║  Node %-2d — Final Submatrix                 ║\n", my_id);
        printf("║  %d rows x %d cols                           \n", my_rows, my_cols);
        printf("╚════════════════════════════════════════════╝\n");
        print_matrix(my_data, my_rows, my_cols);
        printf("\n");
    } else {
        printf("[Node %d] Master holds 0 rows (all distributed to slaves)\n\n", my_id);
    }

    // ============================================================
    // PHASE 2: Compute Order Q Moving Averages (SLAVES ONLY)
    // Each slave appends 2 metadata floats: [node_id, elapsed_time]
    // so its vector is (my_cols + 2) elements. M1PR concatenates
    // everything; master strips metadata after gather.
    // ============================================================
    int q = test_mode ? 3 : compute_q(n);
    float *my_p = NULL;
    int my_p_len = 0;

    if (my_id != 0) {
        // SLAVE: compute MA on transposed submatrix
        // my_rows = number of original columns assigned to this slave
        // my_cols = n = length of each original column (full sequence for MA)
        printf("[Node %d] Computing Order Q=%d Moving Averages on %d columns (each len %d)...\n",
               my_id, q, my_rows, my_cols);

        struct timespec slave_tb, slave_ta;
        clock_gettime(CLOCK_MONOTONIC, &slave_tb);
        my_p = compute_ma(my_data, my_rows, my_cols, q);
        my_p_len = my_rows;  // one p[j] per assigned column
        clock_gettime(CLOCK_MONOTONIC, &slave_ta);

        double slave_elapsed = (slave_ta.tv_sec - slave_tb.tv_sec) +
                               (slave_ta.tv_nsec - slave_tb.tv_nsec) / 1e9;

        printf("[Node %d] Partial vector p (%d elements):\n", my_id, my_p_len);
        print_vector(my_p, my_p_len);
        printf("[Node %d] MA computation time: %.6f sec\n\n", my_id, slave_elapsed);

        // Append metadata: [node_id_as_float, elapsed_as_float]
        my_p = (float *)realloc(my_p, (my_p_len + 2) * sizeof(float));
        my_p[my_p_len]     = (float)my_id;
        my_p[my_p_len + 1] = (float)slave_elapsed;
        my_p_len += 2;
    } else {
        // MASTER: empty p, will be filled by M1PR gather
        my_p = NULL;
        my_p_len = 0;
        printf("[Node 0] Master skips MA computation (no rows held)\n\n");
    }

    // ============================================================
    // PHASE 3: M1PR — Many-to-One Personalized Reduction
    // ============================================================
    gather_tree(my_id, total_nodes, &my_p, &my_p_len, listening_socket, nodes);

    // Master stops timer and prints summary
    struct timespec time_after;
    if (my_id == 0) {
        clock_gettime(CLOCK_MONOTONIC, &time_after);
        double total_elapsed = (time_after.tv_sec - time_before.tv_sec) +
                               (time_after.tv_nsec - time_before.tv_nsec) / 1e9;

        // Parse rebuilt vector: each slave contributed (rows_for_node + 2) elements
        // Layout: [p0...p_{k-1}, node_id, elapsed] per slave, variable k
        double slave_times[MAX_NODES] = {0};
        float *clean_p = (float *)malloc(n * sizeof(float));
        int clean_len = 0;

        int offset = 0;
        for (int s = 1; s < total_nodes; s++) {
            int slave_cols = rows_for_node(s, n, total_nodes);
            if (offset + slave_cols + 2 <= my_p_len) {
                memcpy(&clean_p[clean_len], &my_p[offset], slave_cols * sizeof(float));
                clean_len += slave_cols;
                int sid = (int)my_p[offset + slave_cols];
                double t = (double)my_p[offset + slave_cols + 1];
                slave_times[sid] = t;
                offset += slave_cols + 2;
            }
        }

        // Print rebuilt vector p
        printf("╔════════════════════════════════════════════╗\n");
        printf("║  MASTER — Rebuilt Full Vector p             ║\n");
        printf("║  %d elements                                \n", clean_len);
        printf("╚════════════════════════════════════════════╝\n");
        print_vector(clean_p, clean_len);
        printf("\n");

        // SUMMARY TABLE
        printf("╔══════════════════════════════════════════════════════════╗\n");
        printf("║                    SUMMARY REPORT                       ║\n");
        printf("╠══════════════════════════════════════════════════════════╣\n");
        printf("║  Matrix:        %5d x %-5d                            ║\n", n, n);
        printf("║  Q (MA order):  %-5d                                   ║\n", q);
        printf("║  Total nodes:   %-3d  (1 master + %d slaves)             ║\n", total_nodes, total_nodes - 1);
        printf("║  Strategy:      %-8s %s                        ║\n",
               use_tree ? "TREE" : "LINEAR",
               use_tree ? "O(log n)" : "O(n)    ");
        printf("╠══════════════════════════════════════════════════════════╣\n");
        printf("║  SLAVE COMPUTATION TIMES (MA only):                     ║\n");
        for (int i = 1; i < total_nodes; i++) {
            int sr = rows_for_node(i, n, total_nodes);
            printf("║    Node %-2d:  %.6f sec  (%3d rows)                 ║\n",
                   i, slave_times[i], sr);
        }
        printf("╠══════════════════════════════════════════════════════════╣\n");
        printf("║  MASTER TOTAL TIME:                                     ║\n");
        printf("║    %.6f sec                                          ║\n", total_elapsed);
        printf("║    (1MPB scatter + slave MA + M1PR gather)              ║\n");
        printf("╚══════════════════════════════════════════════════════════╝\n");

        // Verify results in test mode
        if (test_mode && clean_len == 5) {
            float expected[] = {1.414214f, 2.828427f, 4.242641f, 5.656854f, 7.071068f};
            int pass = 1;
            printf("\n╔════════════════════════════════════════════╗\n");
            printf("║  TEST VERIFICATION                         ║\n");
            printf("╠════════════════════════════════════════════╣\n");
            printf("║  Computed: ");
            for (int i = 0; i < 5; i++) printf("%.4f ", clean_p[i]);
            printf("║\n");
            printf("║  Expected: ");
            for (int i = 0; i < 5; i++) printf("%.4f ", expected[i]);
            printf("║\n");
            for (int i = 0; i < 5; i++) {
                if (fabsf(clean_p[i] - expected[i]) > 0.001f) {
                    printf("║  ✗ p[%d] mismatch!                         ║\n", i);
                    pass = 0;
                }
            }
            printf("║  %s                              ║\n",
                   pass ? "✓ ALL TESTS PASSED" : "✗ TESTS FAILED    ");
            printf("╚════════════════════════════════════════════╝\n");
        }

        free(clean_p);
    } else {
        // Slave final report (elapsed already printed above)
        printf("╔════════════════════════════════════════════╗\n");
        printf("║  Node %-2d — SLAVE DONE                      ║\n", my_id);
        printf("╚════════════════════════════════════════════╝\n");
    }

    // Cleanup
    free(my_p);
    if (my_id != 0) {
        free_matrix(my_data, my_rows);
    }
    close(listening_socket);
    printf("[Node %d] Shutting down.\n", my_id);

    return 0;
}