#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 8080

int server_socket;

// Thread function to handle individual client connections
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);  // Free memory allocated for socket descriptor

    printf("Thread created to handle client socket: %d\n", client_sock);

    char buffer[1024];
    int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received:\n%s\n", buffer);
    }

    // Placeholder for future forwarding logic
    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello world";
    send(client_sock, response, strlen(response), 0);

    close(client_sock);
    printf("Closed connection for client socket: %d\n", client_sock);
    return NULL;
}

// Graceful shutdown
void shutdown_handler(int signum) {
    close(server_socket);
    printf("\n[+] Server shutting down gracefully.\n");
    exit(0);
}

int main() {
    signal(SIGINT, shutdown_handler);  // Ctrl+C handling

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    listen(server_socket, 50);
    printf("[+] Server listening on port %d\n", PORT);

    while (1) {
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (*client_sock < 0) {
            perror("Accept failed");
            free(client_sock);
            continue;
        }

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_sock);
        pthread_detach(thread_id);  // Auto-cleanup after thread finishes
    }

    return 0;
}
