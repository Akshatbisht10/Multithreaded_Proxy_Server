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
#include <stdbool.h>

#define PORT 8080
#define BUFFER_SIZE 8192

// Utility: Build cache key for GET/POST
void build_cache_key(const char *method, const char *url, const char *body, char *key, size_t keysize) {
    char normalized_url[1024] = {0};
    char host[512] = {0};
    char path[1024] = "/";
    int port = 80;
    
    printf("[URL DEBUG] Original URL: %s\n", url);
    
    // Parse URL into components
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
        // If no http:// prefix, treat the entire URL as the host
        const char *path_start = strchr(url, '/');
        if (path_start) {
            size_t host_len = path_start - url;
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, url, host_len);
            host[host_len] = '\0';
            strncpy(path, path_start, sizeof(path) - 1);
        } else {
            strncpy(host, url, sizeof(host) - 1);
            strcpy(path, "/");
        }
    }
    
    // Build normalized URL (host + path)
    snprintf(normalized_url, sizeof(normalized_url), "%s%s", host, path);
    printf("[URL DEBUG] Normalized URL: %s\n", normalized_url);
    
    // Build final cache key
    if (strcmp(method, "GET") == 0) {
        strncpy(key, normalized_url, keysize - 1);
        key[keysize - 1] = '\0';
    } else if (strcmp(method, "POST") == 0) {
        snprintf(key, keysize, "%s %s", normalized_url, body ? body : "");
    } else {
        snprintf(key, keysize, "%s %s", method, normalized_url);
    }
    
    printf("[URL DEBUG] Final cache key: %s\n", key);
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

    // Log the request with cache status
    char logbuf[2048];
    char cache_status[32];

    // Build normalized cache key
    char cache_key[BUFFER_SIZE * 2];
    char *body = NULL;
    
    // Skip caching for CONNECT requests
    bool should_cache = (strcmp(method, "CONNECT") != 0);
    
    if (should_cache) {
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

        // Check cache first
        char *cached_response = find_in_cache(cache_key);
        if (cached_response) {
            strcpy(cache_status, "CACHE_HIT");
            // Create combined log message
            snprintf(logbuf, sizeof(logbuf), "%s %s %s | %s", method, url, protocol, cache_status);
            char *request_msg = strdup(logbuf);
            g_idle_add(log_message_idle, request_msg);

            send(client_socket, cached_response, strlen(cached_response), 0);
            free(cached_response);
            close(client_socket);
            return NULL;
        }
        strcpy(cache_status, "CACHE_MISS");
    } else {
        strcpy(cache_status, "CONNECT");
    }

    // Create combined log message
    snprintf(logbuf, sizeof(logbuf), "%s %s %s | %s", method, url, protocol, cache_status);
    char *request_msg = strdup(logbuf);
    g_idle_add(log_message_idle, request_msg);

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

    // Only set up caching for non-CONNECT requests
    char *response = NULL;
    size_t response_capacity = BUFFER_SIZE * 2;  // Start with 16KB
    int offset = 0;
    bool headers_complete = false;
    bool is_success = false;

    if (should_cache) {
        response = malloc(response_capacity);
        if (!response) {
            close(client_socket);
            close(remote_socket);
            return NULL;
        }
    }

    // Set socket timeout to make reads faster
    struct timeval tv;
    tv.tv_sec = 2;  // 2 seconds timeout
    tv.tv_usec = 0;
    setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Use select() for non-blocking reads
    fd_set readfds;
    struct timeval timeout;
    int ready;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(remote_socket, &readfds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        ready = select(remote_socket + 1, &readfds, NULL, NULL, &timeout);
        if (ready <= 0) break;  // Timeout or error
        
        bytes_received = recv(remote_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) break;

        // Forward to client immediately
        send(client_socket, buffer, bytes_received, 0);
        
        // Process for caching if needed
        if (should_cache && response) {
            // Check if we need to grow the buffer
            while (offset + bytes_received >= response_capacity) {
                size_t new_capacity = response_capacity * 2;
                char *new_response = realloc(response, new_capacity);
                if (!new_response) {
                    printf("[CACHE DEBUG] Failed to grow buffer to %zu bytes\n", new_capacity);
                    free(response);
                    response = NULL;
                    goto cleanup;
                }
                response = new_response;
                response_capacity = new_capacity;
                printf("[CACHE DEBUG] Grew buffer to %zu bytes\n", response_capacity);
            }
            
            // Copy new data
            memcpy(response + offset, buffer, bytes_received);
            offset += bytes_received;
            response[offset] = '\0';
            
            // Check headers on first chunk
            if (!headers_complete && strstr(response, "\r\n\r\n")) {
                headers_complete = true;
                if (strstr(response, "HTTP/1.1 200") || strstr(response, "HTTP/1.0 200")) {
                    is_success = true;
                    printf("[CACHE DEBUG] Got successful response, continuing to cache\n");
                } else {
                    printf("[CACHE DEBUG] Not a 200 response, stopping cache\n");
                    free(response);
                    response = NULL;
                    goto cleanup;
                }
            }
        }
    }

    // Cache if we have a complete successful response
    if (should_cache && response && offset > 0 && is_success) {
        printf("[CACHE DEBUG] Caching response of size: %d bytes\n", offset);
        add_to_cache(cache_key, response);
    }

cleanup:
    if (response) free(response);
    close(remote_socket);
    close(client_socket);
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
