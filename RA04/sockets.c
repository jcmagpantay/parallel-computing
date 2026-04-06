#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 2048

// Helper Function: Sets up a listening socket for the given ID based on the base port
int setup_server(int base_port, int my_id) {
    int listening_socket;
    struct sockaddr_in address;
    int opt = 1;

    int my_port = base_port + my_id;

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
    address.sin_port = htons(my_port);

    if (bind(listening_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Since we are building a tree, queue size doesn't need to be huge, but 10 is safe.
    if (listen(listening_socket, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("[Node %d] Listening on port %d...\n", my_id, my_port);
    return listening_socket;
}

// Helper Function: Connects to a specific target node based on ID and sends a message
void send_to_node(int base_port, int target_id, int my_id, char *message) {
    int target_port = base_port + target_id;
    int sock_fd;
    struct sockaddr_in serv_addr;

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("[Node %d] Socket creation error while trying to connect to Node %d\n", my_id, target_id);
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(target_port);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("[Node %d] Invalid address / Address not supported\n", my_id);
        close(sock_fd);
        return;
    }

    // Retry loop in case the target node hasn't started its server yet
    int retries = 10;
    while (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        retries--;
        if (retries == 0) {
            printf("[Node %d] Connection to Node %d (Port %d) Failed after retries.\n", my_id, target_id, target_port);
            close(sock_fd);
            return;
        }
        usleep(500000); // 0.5 seconds
    }

    printf("[Node %d] Successfully connected to Node %d (Port %d). Sending data...\n", my_id, target_id, target_port);
    send(sock_fd, message, strlen(message), 0);
    
    // Close mapping after send finishes.
    close(sock_fd);
}

// Helper Function: Blocks and waits to receive a message from *any* connecting node
void receive_message(int listening_socket, int my_id, char *buffer) {
    int incoming_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    memset(buffer, 0, BUFFER_SIZE);

    printf("[Node %d] Waiting to receive a message...\n", my_id);
    
    if ((incoming_sock = accept(listening_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        return;
    }

    read(incoming_sock, buffer, BUFFER_SIZE);
    
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, sender_ip, INET_ADDRSTRLEN);
    
    printf("[Node %d] Received data: '%s'\n", my_id, buffer);

    close(incoming_sock); 
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <base_port> <my_id> <total_nodes>\n", argv[0]);
        printf("Example (Master): %s 8000 0 8\n", argv[0]);
        printf("Example (Slave 4): %s 8000 4 8\n", argv[0]);
        return -1;
    }

    int base_port = atoi(argv[1]);
    int my_id = atoi(argv[2]);
    int total_nodes = atoi(argv[3]);
    char buffer[BUFFER_SIZE];

    // Node 0 needs data to broadcast initially
    if (my_id == 0) {
        strcpy(buffer, "INITIAL_BROADCAST_DATA_BLOCK");
    }

    // Every node sets up a storefront.
    int listening_socket = setup_server(base_port, my_id);
    
    // ---------------------------------------------------------------------------------
    // DYNAMIC BINOMIAL TREE BROADCAST LOGIC
    // ---------------------------------------------------------------------------------
    
    int has_data = (my_id == 0) ? 1 : 0;
    
    // Calculate the highest power of 2 that is strictly less than total_nodes.
    // E.g., for total_nodes = 8, highest_power_of_2 = 4
    // E.g., for total_nodes = 6, highest_power_of_2 = 4
    int highest_power_of_2 = 1;
    while (highest_power_of_2 < total_nodes) {
        highest_power_of_2 *= 2;
    }
    highest_power_of_2 /= 2;

    // Shrinking stride strategy
    for (int stride = highest_power_of_2; stride > 0; stride /= 2) {
        if (!has_data) {
            // A node receives data at the stride that matches its least-significant factor of 2.
            // Mathematically, if my_id % (stride * 2) == stride, it's my turn to receive.
            if ((my_id % (stride * 2)) == stride) {
                receive_message(listening_socket, my_id, buffer);
                has_data = 1; // Now I have the data! I can forward it on the NEXT stride.
                usleep(500000); // 0.5s pause to ensure my targets start their listening loops
            }
        } else {
            // If I already had data at the beginning of this stride, I am responsible for forwarding it.
            int target_id = my_id + stride;
            
            // We only send if the target_id is actually part of our network
            if (target_id < total_nodes) {
                char output_buffer[BUFFER_SIZE];
                // Optional: We can append to the buffer to prove it passed through this node
                sprintf(output_buffer, "%s->Node_%d", buffer, my_id);
                
                send_to_node(base_port, target_id, my_id, output_buffer);
                
                // Keep the updated path in my own buffer for subsequent sending
                strcpy(buffer, output_buffer);
            }
        }
    }

    if (!has_data) {
        printf("[Node %d] Exiting, never received data.\n", my_id);
    } else {
        printf("[Node %d] Finished task. Final data: '%s'\n", my_id, buffer);
    }
    
    // Clean up
    close(listening_socket);
    printf("[Node %d] Shutting down.\n", my_id);

    return 0;
}