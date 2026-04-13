#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_NODES 64

// =====================================================================
// Node entry from config
// =====================================================================
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int  port;
} NodeEntry;

// =====================================================================
// Reliable send: loops until all bytes are sent
// =====================================================================
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

// =====================================================================
// Reliable recv: loops until all bytes are received
// =====================================================================
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

// =====================================================================
// Setup a listening socket on the given port
// =====================================================================
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

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listening_socket, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return listening_socket;
}

// =====================================================================
// Connect to a target IP:port with retries
// =====================================================================
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

    int retries = 20;
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        retries--;
        if (retries == 0) {
            printf("Connection to %s:%d failed after retries.\n", ip, port);
            close(sock_fd);
            return -1;
        }
        usleep(500000);
    }

    return sock_fd;
}

// =====================================================================
// Read config for LOCAL mode
// Format: "ip base_port" — ports auto-increment per node_id
// =====================================================================
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

// =====================================================================
// Read config for REMOTE mode
// Format: each line is "ip port" — line 0 = node 0 (master), etc.
// =====================================================================
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

// =====================================================================
// Matrix utilities
// =====================================================================
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
    for (int i = 0; i < rows; i++) {
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

// =====================================================================
// Compute how many rows a subtree of nodes should own
// Uses fair distribution: node i gets (n/total) + (i < n%total ? 1 : 0)
// =====================================================================
int rows_for_subtree(int start_node, int subtree_size, int n, int total_nodes) {
    int total = 0;
    for (int i = start_node; i < start_node + subtree_size && i < total_nodes; i++) {
        total += (n / total_nodes) + (i < (n % total_nodes) ? 1 : 0);
    }
    return total;
}

// =====================================================================
// Send a submatrix (rows [start_row, start_row+num_rows)) over a socket
// Wire format: [uint32 rows][uint32 cols][row-major int data in network order]
// =====================================================================
void send_submatrix(int sock, int **data, int start_row, int num_rows, int cols) {
    uint32_t nr = htonl((uint32_t)num_rows);
    uint32_t nc = htonl((uint32_t)cols);
    send_all(sock, &nr, sizeof(nr));
    send_all(sock, &nc, sizeof(nc));

    for (int r = 0; r < num_rows; r++) {
        uint32_t *buf = (uint32_t *)malloc(cols * sizeof(uint32_t));
        for (int c = 0; c < cols; c++) {
            buf[c] = htonl((uint32_t)data[start_row + r][c]);
        }
        send_all(sock, buf, cols * sizeof(uint32_t));
        free(buf);
    }
}

// =====================================================================
// Receive a submatrix from a socket
// =====================================================================
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

// =====================================================================
// MAIN
// =====================================================================
int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    // Usage: ./lab04 <n> <node_id> <mode> <total_nodes>
    if (argc < 5) {
        printf("Usage: %s <n> <node_id> <mode> <total_nodes>\n\n", argv[0]);
        printf("  n           = size of the square matrix (n x n)\n");
        printf("  node_id     = node identifier (0 = master, 1+ = slave)\n");
        printf("  mode        = 'local' or 'remote'\n");
        printf("  total_nodes = total number of participating nodes\n\n");
        printf("Examples (4 nodes, 12x12 matrix):\n");
        printf("  Master:  %s 12 0 local 4\n", argv[0]);
        printf("  Slave 1: %s 12 1 local 4\n", argv[0]);
        printf("  Slave 2: %s 12 2 local 4\n", argv[0]);
        printf("  Slave 3: %s 12 3 local 4\n", argv[0]);
        return -1;
    }

    int n           = atoi(argv[1]);
    int my_id       = atoi(argv[2]);
    char *mode      = argv[3];
    int total_nodes = atoi(argv[4]);

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

    // --- Read config to learn all node addresses ---
    NodeEntry nodes[MAX_NODES];
    int num_nodes;

    if (strcmp(mode, "local") == 0) {
        num_nodes = read_config_local("config.local.txt", nodes, total_nodes);
    } else {
        num_nodes = read_config_remote("config.remote.txt", nodes, MAX_NODES);

        // SSH proof for slaves running in remote mode
        if (my_id != 0) {
            const char *ssh_conn = getenv("SSH_CONNECTION");
            const char *ssh_client = getenv("SSH_CLIENT");
            if (ssh_conn) {
                printf("[Node %d] *** SSH PROOF: SSH_CONNECTION = %s\n", my_id, ssh_conn);
                printf("[Node %d] *** SSH PROOF: SSH_CLIENT     = %s\n", my_id,
                       ssh_client ? ssh_client : "(not set)");
            }
        }
    }

    if (num_nodes < 0) return -1;

    int my_port = nodes[my_id].port;

    printf("============================================\n");
    if (my_id == 0)
        printf("  MASTER NODE (Node 0)\n");
    else
        printf("  SLAVE NODE (Node %d)\n", my_id);
    printf("  Matrix: %d x %d | Port: %d\n", n, n, my_port);
    printf("  Mode: %s | Total Nodes: %d\n", mode, total_nodes);
    printf("  Strategy: Binomial Tree Scatter O(log n)\n");
    printf("============================================\n\n");

    // --- Setup listening socket ---
    int listening_socket = setup_server(my_port);
    printf("[Node %d] Listening on port %d\n\n", my_id, my_port);

    // --- Matrix data ---
    int **my_data = NULL;
    int my_rows = 0;
    int cols = n;
    int has_data = 0;

    // --- Master creates the matrix ---
    if (my_id == 0) {
        srand((unsigned int)time(NULL));
        my_data = create_matrix(n);
        my_rows = n;
        has_data = 1;

        printf("[Node 0] Generated %d x %d matrix M:\n", n, n);
        print_matrix(my_data, my_rows, cols);
        printf("\n");
    }

    // --- Timing ---
    struct timespec time_before, time_after;
    int time_started = 0;

    if (my_id == 0) {
        clock_gettime(CLOCK_MONOTONIC, &time_before);
        time_started = 1;
    }

    // =================================================================
    // BINOMIAL TREE SCATTER — O(log n) distribution
    //
    // At each stride, nodes that have data split their portion:
    //   - Keep the top half (rows for my subtree)
    //   - Send the bottom half to (my_id + stride)
    //
    // Nodes that don't have data yet check if they should receive.
    // A node with id X receives at the stride where X % (2*stride) == stride.
    //
    // Example with 4 nodes, 12 rows:
    //   stride=2: Node 0 sends rows 6-11 to Node 2. Keeps 0-5.
    //   stride=1: Node 0 sends rows 3-5 to Node 1. Keeps 0-2.
    //             Node 2 sends rows 9-11 to Node 3. Keeps 6-8.
    //   Result: Node0=0-2, Node1=3-5, Node2=6-8, Node3=9-11
    // =================================================================

    int hp2 = 1;
    while (hp2 < total_nodes) hp2 *= 2;

    for (int stride = hp2 / 2; stride > 0; stride /= 2) {
        if (has_data) {
            int target = my_id + stride;
            if (target < total_nodes) {
                // Compute how many rows the target subtree needs
                int target_subtree_size = stride;
                if (target + stride > total_nodes)
                    target_subtree_size = total_nodes - target;
                int send_rows = rows_for_subtree(target, target_subtree_size, n, total_nodes);
                int keep_rows = my_rows - send_rows;

                printf("[Node %d] Stride %d: Sending %d rows to Node %d (keeping %d)\n",
                       my_id, stride, send_rows, target, keep_rows);

                // Print what we're sending
                printf("[Node %d] Submatrix for Node %d:\n", my_id, target);
                print_matrix(&my_data[keep_rows], send_rows, cols);

                // Connect to target and send the bottom half
                int sock = connect_to(nodes[target].ip, nodes[target].port);
                if (sock < 0) {
                    printf("[Node %d] ERROR: Failed to connect to Node %d!\n", my_id, target);
                    return -1;
                }
                send_submatrix(sock, my_data, keep_rows, send_rows, cols);
                close(sock);

                // Free the sent rows and shrink
                for (int r = keep_rows; r < my_rows; r++) {
                    free(my_data[r]);
                }
                my_rows = keep_rows;
                my_data = (int **)realloc(my_data, my_rows * sizeof(int *));

                printf("[Node %d] Now holding %d rows\n\n", my_id, my_rows);
            }
        } else {
            // Check if I should receive in this round
            // Node X receives when X % (2*stride) == stride
            if ((my_id % (stride * 2)) == stride) {
                printf("[Node %d] Stride %d: Waiting to receive from parent...\n", my_id, stride);

                struct sockaddr_in addr;
                int addrlen = sizeof(addr);
                int conn = accept(listening_socket, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
                if (conn < 0) { perror("Accept failed"); return -1; }

                // Start timing for slaves when data first arrives
                if (!time_started) {
                    clock_gettime(CLOCK_MONOTONIC, &time_before);
                    time_started = 1;
                }

                my_data = recv_submatrix(conn, &my_rows, &cols);
                close(conn);
                has_data = 1;

                printf("[Node %d] Received %d rows x %d cols\n\n", my_id, my_rows, cols);
            }
        }

        // Brief pause between rounds for synchronization
        usleep(200000); // 200ms
    }

    // =================================================================
    // POST-SCATTER: Print results, acks, timing
    // =================================================================

    printf("[Node %d] === Final submatrix (%d rows x %d cols) ===\n", my_id, my_rows, cols);
    print_matrix(my_data, my_rows, cols);
    printf("\n");

    // Slaves send "ack" to master
    if (my_id != 0) {
        printf("[Node %d] Sending ack to Master (Node 0) at %s:%d...\n",
               my_id, nodes[0].ip, nodes[0].port);
        int sock = connect_to(nodes[0].ip, nodes[0].port);
        if (sock >= 0) {
            send_all(sock, "ack", 3);
            close(sock);
            printf("[Node %d] Ack sent.\n", my_id);
        }

        // Record time_after for slave (after sending ack)
        clock_gettime(CLOCK_MONOTONIC, &time_after);
    }

    // Master collects acks from all slaves
    if (my_id == 0) {
        printf("[Node 0] Waiting for acks from %d slaves...\n", total_nodes - 1);
        for (int i = 0; i < total_nodes - 1; i++) {
            struct sockaddr_in addr;
            int addrlen = sizeof(addr);
            int conn = accept(listening_socket, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
            if (conn < 0) { perror("Accept failed"); continue; }

            char ack_buf[16] = {0};
            recv_all(conn, ack_buf, 3);
            close(conn);
            printf("[Node 0] Received ack %d/%d\n", i + 1, total_nodes - 1);
        }

        // Record time_after for master (after all acks)
        clock_gettime(CLOCK_MONOTONIC, &time_after);
        printf("[Node 0] All %d slaves acknowledged.\n\n", total_nodes - 1);
    }

    // Print elapsed time
    double time_elapsed = (time_after.tv_sec - time_before.tv_sec) +
                          (time_after.tv_nsec - time_before.tv_nsec) / 1e9;
    printf("[Node %d] time_elapsed = %f sec\n", my_id, time_elapsed);

    // Cleanup
    free_matrix(my_data, my_rows);
    close(listening_socket);
    printf("[Node %d] Shutting down.\n", my_id);

    return 0;
}