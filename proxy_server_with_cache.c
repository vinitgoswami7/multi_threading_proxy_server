#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 8080

int server_socket;

#define BUFFER_SIZE 8192

// --- Logging ---
void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}

void log_request(const char* client_ip, const char* request_buffer) {
    FILE* logfile = fopen("proxy.log", "a");
    if (!logfile) return;

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char host[256] = "UNKNOWN";
    if (strncmp(request_buffer, "CONNECT", 7) == 0) {
        sscanf(request_buffer, "CONNECT %255s", host);
    } else {
        const char* host_header = strstr(request_buffer, "Host: ");
        if (host_header) sscanf(host_header, "Host: %255s", host);
    }

    fprintf(logfile, "[%s] %s requested %s\n", timestamp, client_ip, host);
    fclose(logfile);
}

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


int extract_host_port(const char *request, char *host, int *port) {
    const char *host_start = strstr(request, "Host: ");
    if (!host_start) return -1;

    host_start += 6;
    const char *host_end = strstr(host_start, "\r\n");
    if (!host_end) return -1;

    size_t host_len = host_end - host_start;
    strncpy(host, host_start, host_len);
    host[host_len] = '\0';

    // Default HTTP port
    *port = 80;

    // If port is included
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        *port = atoi(colon + 1);
    }

    return 0;
}

bool is_blacklisted(const char* host) {
    FILE* file = fopen("blacklist.txt", "r");
    if (!file) return false;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0; // Trim newline
        if (strstr(host, line)) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}




// Graceful shutdown
void shutdown_handler(int signum) {
    close(server_socket);
    printf("\n[+] Server shutting down gracefully.\n");
    exit(0);
}

// --- Client Handler ---
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(client_sock);
        return NULL;
    }
    buffer[len] = '\0';

    // Get client IP
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    getpeername(client_sock, (struct sockaddr*)&client_addr, &addrlen);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    // Extract host
    char host[256] = "UNKNOWN";
    if (strncmp(buffer, "CONNECT", 7) == 0) {
        sscanf(buffer, "CONNECT %255s", host);
    } else {
        char* host_header = strstr(buffer, "Host: ");
        if (host_header) sscanf(host_header, "Host: %255s", host);
    }

    // 🔒 Blacklist Check
    if (is_blacklisted(host)) {
        printf("❌ Blocked blacklisted domain: %s\n", host);
        log_request(client_ip, buffer);
        send_403(client_sock);
        close(client_sock);
        return NULL;
    }

    // ✅ Log and Forward
    log_request(client_ip, buffer);
    forward_request(client_sock, buffer);

    close(client_sock);
    return NULL;
}
    // Forward client request to server
    send(server_sock, buffer, bytes_received, 0);

    // Relay response from server back to client
    while ((bytes_received = recv(server_sock, buffer, sizeof(buffer), 0)) > 0) {
        send(client_sock, buffer, bytes_received, 0);
    }

    close(server_sock);
    close(client_sock);
    return NULL;
    
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
