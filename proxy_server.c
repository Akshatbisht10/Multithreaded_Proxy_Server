#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>

#define PORT 8080
#define BUFFER_SIZE 8192

const char *blocked_domains[] = {
    "www.blocked.com",
    "example-bad-site.com",
    "www.wikipedia.org"
};
int blocked_count = 3;

int is_blocked(const char *host) {
    for (int i = 0; i < blocked_count; ++i) {
        if (strstr(host, blocked_domains[i])) {
            return 1;
        }
    }
    return 0;
}

int connect_to_host(const char *host, int port) {
    struct hostent *server = gethostbyname(host);
    if (!server) return -1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void tunnel_data(int client_fd, int remote_fd) {
    char buffer[BUFFER_SIZE];
    fd_set fds;
    while (1) {
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);
        FD_SET(remote_fd, &fds);
        int maxfd = client_fd > remote_fd ? client_fd : remote_fd;
        if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(client_fd, &fds)) {
            int n = recv(client_fd, buffer, BUFFER_SIZE, 0);
            if (n <= 0) break;
            send(remote_fd, buffer, n, 0);
        }
        if (FD_ISSET(remote_fd, &fds)) {
            int n = recv(remote_fd, buffer, BUFFER_SIZE, 0);
            if (n <= 0) break;
            send(client_fd, buffer, n, 0);
        }
    }
    close(remote_fd);
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return NULL;
    }
    buffer[bytes_received] = '\0';

    char method[16], url[1024], protocol[16];
    if (sscanf(buffer, "%15s %1023s %15s", method, url, protocol) != 3) {
        fprintf(stderr, "[-] Failed to parse request line: %s\n", buffer);
        close(client_socket);
        return NULL;
    }

    printf("[+] Method: %s | URL: %s | Protocol: %s\n", method, url, protocol);

    // HTTPS tunneling (CONNECT method)
    if (strcmp(method, "CONNECT") == 0) {
        char host[512] = {0};
        int port = 443;

        char *colon = strchr(url, ':');
        if (!colon) {
            fprintf(stderr, "[-] Malformed CONNECT URL: %s\n", url);
            close(client_socket);
            return NULL;
        }

        size_t host_len = colon - url;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, url, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);

        printf("[+] HTTPS Request to host: %s:%d\n", host, port);

        if (is_blocked(host)) {
            const char *forbidden = "HTTP/1.1 403 Forbidden\r\n\r\n";
            send(client_socket, forbidden, strlen(forbidden), 0);
            close(client_socket);
            return NULL;
        }

        int remote_socket = connect_to_host(host, port);
        if (remote_socket < 0) {
            const char *fail_msg = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
            send(client_socket, fail_msg, strlen(fail_msg), 0);
            close(client_socket);
            return NULL;
        }

        const char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client_socket, connection_established, strlen(connection_established), 0);
        tunnel_data(client_socket, remote_socket);
        close(client_socket);
        return NULL;
    }

    // HTTP requests (GET, POST, etc.)
    char host[512] = {0};
    char path[1024] = "/";
    int port = 80;

    if (strncmp(url, "http://", 7) == 0) {
        const char *host_start = url + 7;
        const char *path_start = strchr(host_start, '/');

        if (path_start) {
            size_t host_len = path_start - host_start;
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, host_start, host_len);
            strncpy(path, path_start, sizeof(path) - 1);
        } else {
            strncpy(host, host_start, sizeof(host) - 1);
            strcpy(path, "/");
        }
    } else {
        fprintf(stderr, "[-] Malformed HTTP URL: %s\n", url);
        close(client_socket);
        return NULL;
    }

    printf("[+] HTTP Request to host: %s | Path: %s\n", host, path);

    if (is_blocked(host)) {
        const char *forbidden = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, forbidden, strlen(forbidden), 0);
        close(client_socket);
        return NULL;
    }

    int remote_socket = connect_to_host(host, port);
    if (remote_socket < 0) {
        close(client_socket);
        return NULL;
    }

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n",
             method, path, protocol, host);
    send(remote_socket, request, strlen(request), 0);

    while ((bytes_received = recv(remote_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, bytes_received, 0);
    }

    close(remote_socket);
    close(client_socket);
    return NULL;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Proxy server listening on port %d...\n", PORT);

    while (1) {
        socklen_t addrlen = sizeof(address);
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (*client_socket < 0) {
            perror("Accept failed");
            free(client_socket);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
