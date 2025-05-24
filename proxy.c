#include "proxy.h"
#include "cache.h"
#include "gui.h"
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

// Utility: Build cache key for GET/POST
void build_cache_key(const char *method, const char *url, const char *body, char *key, size_t keysize) {
    if (strcmp(method, "POST") == 0) {
        snprintf(key, keysize, "%s %s %s", method, url, body ? body : "");
    } else {
        snprintf(key, keysize, "%s %s", method, url);
    }
}

// Check if the host is blocked
int is_blocked(const char *host) {
    const char *blocked_domains[] = {
        "www.blocked.com",
        "example-bad-site.com",
        "www.wikipedia.org"
    };
    for (int i = 0; i < 3; ++i) {
        if (strstr(host, blocked_domains[i])) {
            return 1;
        }
    }
    return 0;
}

// Connect to the remote host
static int connect_to_host(const char *host, int port) {
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

// Tunnel HTTP/HTTPS data
static void tunnel_data(int client_fd, int remote_fd) {
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

// Handle HTTP/HTTPS requests
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE+1];
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return NULL;
    }
    buffer[bytes_received] = '\0';

    char method[16], url[1024], protocol[16];
    if (sscanf(buffer, "%15s %1023s %15s", method, url, protocol) != 3) {
        close(client_socket);
        return NULL;
    }

    // Log the request
    char logbuf[2048];
    snprintf(logbuf, sizeof(logbuf), "[+] Method: %s | URL: %s | Protocol: %s", method, url, protocol);
    char *msg = strdup(logbuf);
    g_idle_add(log_message_idle, msg);

    // Handle HTTPS requests (CONNECT method)
    if (strcmp(method, "CONNECT") == 0) {
        char host[512] = {0};
        int port = 443;

        char *colon = strchr(url, ':');
        if (!colon) {
            close(client_socket);
            return NULL;
        }

        size_t host_len = colon - url;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, url, host_len);
        host[host_len] = '\0';

        port = atoi(colon + 1);

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

    // Handle HTTP requests (GET, POST, etc.)
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
            host[host_len] = '\0';
            strncpy(path, path_start, sizeof(path) - 1);
        } else {
            strncpy(host, host_start, sizeof(host) - 1);
            strcpy(path, "/");
        }
    } else {
        strncpy(host, url, sizeof(host) - 1);
        strcpy(path, "/");
    }

    if (is_blocked(host)) {
        const char *forbidden = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, forbidden, strlen(forbidden), 0);
        close(client_socket);
        return NULL;
    }

    // Build normalized cache key
    char cache_key[BUFFER_SIZE * 2];
    char *body = NULL;
    if (strcmp(method, "POST") == 0) {
        char *body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) {
            body = body_start + 4;
        } else {
            body = "";
        }
        build_cache_key(method, url, body, cache_key, sizeof(cache_key));
    } else {
        build_cache_key(method, url, NULL, cache_key, sizeof(cache_key));
    }

    // Cache check
    char *cached_response = find_in_cache(cache_key);
    if (cached_response) {
        log_cache_event(cache_key, 1);
        send(client_socket, cached_response, strlen(cached_response), 0);
        close(client_socket);
        return NULL;
    }
    log_cache_event(cache_key, 0);

    int remote_socket = connect_to_host(host, port);
    if (remote_socket < 0) {
        close(client_socket);
        return NULL;
    }

    // Forward client request to remote server
    char request[BUFFER_SIZE*2];
    snprintf(request, sizeof(request), "%s %s %s\r\n", method, path, protocol);

    // Find end of request line
    char *headers_start = strstr(buffer, "\r\n");
    if (headers_start) {
        strncat(request, headers_start + 2, sizeof(request) - strlen(request) - 1);
    }
    send(remote_socket, request, strlen(request), 0);

    // Receive response and forward to client, also cache
    int response_size = BUFFER_SIZE * 10;
    char *response = malloc(response_size); // up to ~80KB
    int offset = 0;
    while ((bytes_received = recv(remote_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        send(client_socket, buffer, bytes_received, 0);
        if (offset + bytes_received < response_size) {
            memcpy(response + offset, buffer, bytes_received);
            offset += bytes_received;
        }
    }
    if (offset > 0) {
        response[offset] = '\0';
        add_to_cache(cache_key, response);
    }
    free(response);

    close(client_socket);
    close(remote_socket);
    return NULL;
}

// Server thread function
void* server_thread_func(void* arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[-] Unable to create socket");
        return NULL;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[-] Bind failed");
        return NULL;
    }

    if (listen(server_fd, 100) < 0) { // increased backlog for stress test
        perror("[-] Listen failed");
        return NULL;
    }

    char *msg = strdup("[+] Proxy server running on port 8080");
    g_idle_add(log_message_idle, msg);

    while (1) {
        int *client_fd = (int *)malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            perror("[-] Accept failed");
            free(client_fd);
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }

    close(server_fd);
    return NULL;
}
