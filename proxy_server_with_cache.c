#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <time.h>
#include <stdbool.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8080
#define BUFFER_SIZE 8192

int server_sock;
bool keep_running = true;

void shutdown_server(int signum) {
    printf("\n🔴 Shutdown signal received. Closing server socket...\n");
    keep_running = false;
    close(server_sock);
    exit(0);
}

void sha1_hash(const char* input, char* output) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input, strlen(input), hash);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA_DIGEST_LENGTH * 2] = '\0';
}

bool serve_from_cache(const char* url, int client_sock) {
    char hash[SHA_DIGEST_LENGTH * 2 + 1];
    sha1_hash(url, hash);

    char filename[512];
    snprintf(filename, sizeof(filename), "cache/%s", hash);

    FILE* file = fopen(filename, "rb");
    if (!file) return false;

    char buffer[BUFFER_SIZE];
    int n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_sock, buffer, n, 0);
    }

    fclose(file);
    printf("✅ Served from cache: %s\n", url);
    return true;
}

void cache_response(const char* url, const char* response, size_t size) {
    char hash[SHA_DIGEST_LENGTH * 2 + 1];
    sha1_hash(url, hash);

    char filename[512];
    snprintf(filename, sizeof(filename), "cache/%s", hash);

    FILE* file = fopen(filename, "wb");
    if (!file) return;

    fwrite(response, 1, size, file);
    fclose(file);

    printf("💾 Cached: %s\n", url);
}

void forward_request(int client_sock, const char* request) {
    char host[256];
    int port = 80;
    sscanf(strstr(request, "Host: ") + 6, "%s", host);
    strtok(host, "\r\n");

    char url[512];
    sscanf(request, "GET %511s", url);

    if (strncmp(request, "GET", 3) == 0 && serve_from_cache(url, client_sock)) {
        return;
    }

    struct hostent* server = gethostbyname(host);
    if (!server) return;

    int remote_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    memcpy(&remote_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(remote_sock, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
        close(remote_sock);
        return;
    }

    send(remote_sock, request, strlen(request), 0);

    char buffer[BUFFER_SIZE];
    char* full_response = malloc(10 * BUFFER_SIZE);
    size_t total = 0;

    int n;
    while ((n = recv(remote_sock, buffer, sizeof(buffer), 0)) > 0) {
        send(client_sock, buffer, n, 0);
        if (total + n < 10 * BUFFER_SIZE) {
            memcpy(full_response + total, buffer, n);
            total += n;
        }
    }

    if (strncmp(request, "GET", 3) == 0) {
        cache_response(url, full_response, total);
    }

    free(full_response);
    close(remote_sock);
}

void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        perror("recv failed");
        close(client_sock);
        return NULL;
    }

    buffer[bytes_received] = '\0';
    forward_request(client_sock, buffer);
    close(client_sock);
    return NULL;
}

int main() {
    signal(SIGINT, shutdown_server);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 10);

    printf("🚀 Proxy server running on port %d... (Ctrl+C to stop)\n", PORT);
    mkdir("cache", 0777);

    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);

        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, client_sock);
        pthread_detach(thread_id);
    }

    close(server_sock);
    return 0;
}
