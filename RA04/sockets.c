#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define BUFFER_SIZE 2048
#define MAX_SLAVES  64

// =====================================================================
// Slave entry from config
// =====================================================================
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int  port;
} SlaveEntry;

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

    // Retry loop in case the target hasn't started listening yet
    int retries = 20;
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        retries--;
        if (retries == 0) {
            printf("Connection to %s:%d failed after retries.\n", ip, port);
            close(sock_fd);
            return -1;
        }
        usleep(500000); // 0.5 seconds
    }

    return sock_fd;
}

// =====================================================================
// Read config for LOCAL mode
// Format: single line "ip base_port"
// Slave ports are auto-incremented: base_port+1, base_port+2, ...
// =====================================================================
int read_config_local(const char *filename, char *master_ip, int *base_port,
                      SlaveEntry slaves[], int t) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Cannot open config.local.txt");
        return -1;
    }

    if (fscanf(f, "%s %d", master_ip, base_port) != 2) {
        printf("Error: Invalid config.local.txt format. Expected: <ip> <base_port>\n");
        fclose(f);
        return -1;
    }
    fclose(f);

    // Auto-generate slave entries: base_port+1, base_port+2, ..., base_port+t
    for (int i = 0; i < t; i++) {
        strncpy(slaves[i].ip, master_ip, INET_ADDRSTRLEN);
        slaves[i].port = (*base_port) + (i + 1);
    }

    return t;
}

// =====================================================================
// Read config for REMOTE mode
// Format:
//   Line 1: master_ip
//   Lines 2+: slave_ip port
// =====================================================================
int read_config_remote(const char *filename, char *master_ip,
                       SlaveEntry slaves[], int max_slaves) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Cannot open config.remote.txt");
        return -1;
    }

    if (fscanf(f, "%s", master_ip) != 1) {
        printf("Error: Could not read master IP from config.remote.txt\n");
        fclose(f);
        return -1;
    }

    int count = 0;
    while (count < max_slaves &&
           fscanf(f, "%s %d", slaves[count].ip, &slaves[count].port) == 2) {
        count++;
    }
    fclose(f);

    return count;
}

// =====================================================================
// Create an n x n matrix with random positive integers (1-100)
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

// =====================================================================
// Print a matrix (or submatrix) with given rows and cols
// =====================================================================
void print_matrix(int **M, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%4d", M[i][j]);
            if (j < cols - 1) printf(" ");
        }
        printf("\n");
    }
}

// =====================================================================
// Free an allocated matrix
// =====================================================================
void free_matrix(int **M, int rows) {
    for (int i = 0; i < rows; i++) {
        free(M[i]);
    }
    free(M);
}

// =====================================================================
// MASTER LOGIC
// =====================================================================
void run_master(int n, int port, const char *mode, int t) {
    char master_ip[INET_ADDRSTRLEN];
    int base_port = 0;
    SlaveEntry slaves[MAX_SLAVES];
    int num_slaves = 0;

    // --- Step 1: Read config ---
    if (strcmp(mode, "local") == 0) {
        num_slaves = read_config_local("config.local.txt", master_ip, &base_port, slaves, t);
    } else {
        num_slaves = read_config_remote("config.remote.txt", master_ip, slaves, MAX_SLAVES);
    }

    if (num_slaves <= 0) {
        printf("[Master] Error: No slaves found in config.\n");
        return;
    }

    // In local mode, t was provided; in remote mode, t = num_slaves from config
    if (strcmp(mode, "remote") == 0) {
        t = num_slaves;
    }

    printf("[Master] Number of slaves: %d\n", t);

    // --- Step 2: Create the n x n matrix ---
    srand((unsigned int)time(NULL));
    int **M = create_matrix(n);

    printf("[Master] Generated %d x %d matrix M:\n", n, n);
    print_matrix(M, n, n);
    printf("\n");

    // --- Step 3: Partition the matrix into t submatrices ---
    // Each submatrix has (n/t) rows; last slave gets the remainder
    int base_rows = n / t;
    int remainder  = n % t;

    // --- Step 4: Record time_before ---
    struct timespec time_before, time_after;
    clock_gettime(CLOCK_MONOTONIC, &time_before);

    // --- Step 5: Distribute submatrices to slaves ---
    int row_offset = 0;
    for (int i = 0; i < t; i++) {
        int rows_for_slave = base_rows + (i < remainder ? 1 : 0);
        int cols = n;

        printf("[Master] Connecting to Slave %d at %s:%d (sending %d rows)...\n",
               i + 1, slaves[i].ip, slaves[i].port, rows_for_slave);

        // Print the submatrix being sent to this slave
        printf("[Master] Submatrix for Slave %d (rows %d-%d):\n",
               i + 1, row_offset, row_offset + rows_for_slave - 1);
        print_matrix(&M[row_offset], rows_for_slave, cols);

        int sock = connect_to(slaves[i].ip, slaves[i].port);
        if (sock < 0) {
            printf("[Master] Failed to connect to Slave %d. Aborting.\n", i + 1);
            free_matrix(M, n);
            return;
        }

        // Send dimensions: rows, cols (as network-byte-order uint32)
        uint32_t net_rows = htonl((uint32_t)rows_for_slave);
        uint32_t net_cols = htonl((uint32_t)cols);
        send_all(sock, &net_rows, sizeof(net_rows));
        send_all(sock, &net_cols, sizeof(net_cols));

        // Send the submatrix data row by row (each row is cols * sizeof(int) bytes)
        for (int r = 0; r < rows_for_slave; r++) {
            // Convert each element to network byte order
            uint32_t *row_buf = (uint32_t *)malloc(cols * sizeof(uint32_t));
            for (int c = 0; c < cols; c++) {
                row_buf[c] = htonl((uint32_t)M[row_offset + r][c]);
            }
            send_all(sock, row_buf, cols * sizeof(uint32_t));
            free(row_buf);
        }

        // --- Step 6: Wait for acknowledgment from this slave ---
        char ack_buf[16];
        memset(ack_buf, 0, sizeof(ack_buf));
        recv_all(sock, ack_buf, 3); // Receive "ack"

        printf("[Master] Received acknowledgment from Slave %d: \"%s\"\n", i + 1, ack_buf);

        close(sock);
        row_offset += rows_for_slave;
    }

    // --- Step 7: Record time_after ---
    clock_gettime(CLOCK_MONOTONIC, &time_after);

    // --- Step 8: Compute and print elapsed time ---
    double time_elapsed = (time_after.tv_sec - time_before.tv_sec) +
                          (time_after.tv_nsec - time_before.tv_nsec) / 1e9;

    printf("\n[Master] All %d slaves acknowledged.\n", t);
    printf("[Master] time_elapsed = %f sec\n", time_elapsed);

    // Cleanup
    free_matrix(M, n);
}

// =====================================================================
// SLAVE LOGIC
// =====================================================================
void run_slave(int n, int port, const char *mode) {
    char master_ip[INET_ADDRSTRLEN];

    // --- Step 1: Determine master IP from config ---
    if (strcmp(mode, "remote") == 0) {
        SlaveEntry dummy[MAX_SLAVES];
        read_config_remote("config.remote.txt", master_ip, dummy, MAX_SLAVES);
        printf("[Slave] Master IP (from config): %s\n", master_ip);

        // Prove this is running inside an SSH session
        const char *ssh_conn = getenv("SSH_CONNECTION");
        const char *ssh_client = getenv("SSH_CLIENT");
        const char *ssh_tty = getenv("SSH_TTY");
        if (ssh_conn) {
            printf("[Slave] *** SSH PROOF: SSH_CONNECTION = %s\n", ssh_conn);
            printf("[Slave] *** SSH PROOF: SSH_CLIENT     = %s\n", ssh_client ? ssh_client : "(not set)");
            printf("[Slave] *** SSH PROOF: SSH_TTY         = %s\n", ssh_tty ? ssh_tty : "(not set)");
        } else {
            printf("[Slave] WARNING: Not running inside an SSH session!\n");
        }
    } else {
        // In local mode, master IP is from config.local.txt (same machine)
        int base_port;
        SlaveEntry dummy[MAX_SLAVES];
        read_config_local("config.local.txt", master_ip, &base_port, dummy, 0);
        printf("[Slave] Master IP (local): %s\n", master_ip);
    }

    // --- Step 2: Listen on assigned port ---
    int listening_socket = setup_server(port);
    printf("[Slave] Listening on port %d, waiting for master...\n", port);

    // --- Step 3: Accept connection from master ---
    struct sockaddr_in master_addr;
    int addrlen = sizeof(master_addr);
    int conn = accept(listening_socket, (struct sockaddr *)&master_addr, (socklen_t *)&addrlen);
    if (conn < 0) {
        perror("[Slave] Accept failed");
        close(listening_socket);
        return;
    }

    // --- Step 4: Record time_before ---
    struct timespec time_before, time_after;
    clock_gettime(CLOCK_MONOTONIC, &time_before);

    // --- Step 5: Receive submatrix dimensions ---
    uint32_t net_rows, net_cols;
    recv_all(conn, &net_rows, sizeof(net_rows));
    recv_all(conn, &net_cols, sizeof(net_cols));
    int rows = (int)ntohl(net_rows);
    int cols = (int)ntohl(net_cols);

    printf("[Slave] Receiving submatrix: %d rows x %d cols\n", rows, cols);

    // --- Step 6: Receive submatrix data ---
    int **submatrix = (int **)malloc(rows * sizeof(int *));
    for (int r = 0; r < rows; r++) {
        submatrix[r] = (int *)malloc(cols * sizeof(int));
        uint32_t *row_buf = (uint32_t *)malloc(cols * sizeof(uint32_t));
        recv_all(conn, row_buf, cols * sizeof(uint32_t));
        for (int c = 0; c < cols; c++) {
            submatrix[r][c] = (int)ntohl(row_buf[c]);
        }
        free(row_buf);
    }

    // --- Step 7: Send acknowledgment ---
    send_all(conn, "ack", 3);

    // --- Step 8: Record time_after ---
    clock_gettime(CLOCK_MONOTONIC, &time_after);

    // --- Step 9: Print received submatrix for verification ---
    printf("[Slave] Received submatrix:\n");
    print_matrix(submatrix, rows, cols);

    // --- Step 10: Compute and print elapsed time ---
    double time_elapsed = (time_after.tv_sec - time_before.tv_sec) +
                          (time_after.tv_nsec - time_before.tv_nsec) / 1e9;
    printf("[Slave] time_elapsed = %f sec\n", time_elapsed);

    // Cleanup
    free_matrix(submatrix, rows);
    close(conn);
    close(listening_socket);
}

// =====================================================================
// MAIN
// =====================================================================
int main(int argc, char *argv[]) {
    // Usage:
    //   Master: ./lab04 <n> <p> 0 <mode> <t>
    //   Slave:  ./lab04 <n> <p> 1 <mode>
    if (argc < 5) {
        printf("Usage:\n");
        printf("  Master: %s <n> <p> 0 <mode> <t>\n", argv[0]);
        printf("  Slave:  %s <n> <p> 1 <mode>\n", argv[0]);
        printf("\n");
        printf("  n    = size of the square matrix (n x n)\n");
        printf("  p    = port number for this instance\n");
        printf("  s    = status (0 = master, 1 = slave)\n");
        printf("  mode = 'local' or 'remote'\n");
        printf("  t    = number of slaves (master only)\n");
        printf("\n");
        printf("Examples:\n");
        printf("  Master (local, 3 slaves): %s 6 8000 0 local 3\n", argv[0]);
        printf("  Slave  (local, port 8001): %s 6 8001 1 local\n", argv[0]);
        return -1;
    }

    int n    = atoi(argv[1]);  // Matrix size
    int p    = atoi(argv[2]);  // Port number
    int s    = atoi(argv[3]);  // Status: 0=master, 1=slave
    char *mode = argv[4];      // "local" or "remote"

    if (n <= 0) {
        printf("Error: n must be a positive integer.\n");
        return -1;
    }

    if (strcmp(mode, "local") != 0 && strcmp(mode, "remote") != 0) {
        printf("Error: mode must be 'local' or 'remote'.\n");
        return -1;
    }

    if (s == 0) {
        // Master requires t (number of slaves)
        if (argc < 6) {
            printf("Error: Master requires <t> (number of slaves) as 5th argument.\n");
            printf("Usage: %s <n> <p> 0 <mode> <t>\n", argv[0]);
            return -1;
        }
        int t = atoi(argv[5]);
        if (t <= 0) {
            printf("Error: t (number of slaves) must be a positive integer.\n");
            return -1;
        }
        if (t > n) {
            printf("Error: t (number of slaves) cannot exceed n (matrix size).\n");
            return -1;
        }
        printf("============================================\n");
        printf("  MASTER NODE\n");
        printf("  Matrix: %d x %d | Port: %d | Slaves: %d\n", n, n, p, t);
        printf("  Mode: %s\n", mode);
        printf("============================================\n\n");
        run_master(n, p, mode, t);
    } else if (s == 1) {
        printf("============================================\n");
        printf("  SLAVE NODE\n");
        printf("  Matrix: %d x %d | Port: %d\n", n, n, p);
        printf("  Mode: %s\n", mode);
        printf("============================================\n\n");
        run_slave(n, p, mode);
    } else {
        printf("Error: s must be 0 (master) or 1 (slave).\n");
        return -1;
    }

    return 0;
}