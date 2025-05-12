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
#include <gtk/gtk.h>

#define PORT 8080
#define BUFFER_SIZE 8192
#define CACHE_SIZE 10

// Struct for cache nodes
typedef struct CacheNode {
    char *url;
    char *response;
    struct CacheNode *prev, *next;
} CacheNode;

CacheNode *head = NULL, *tail = NULL;
int cache_count = 0;

// Create a new cache node
CacheNode* create_node(const char *url, const char *response) {
    CacheNode *node = (CacheNode *)malloc(sizeof(CacheNode));
    node->url = strdup(url);
    node->response = strdup(response);
    node->prev = node->next = NULL;
    return node;
}

// Move node to head
void move_to_head(CacheNode *node) {
    if (node == head) return;
    if (node == tail) tail = tail->prev;
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    node->next = head;
    node->prev = NULL;
    if (head) head->prev = node;
    head = node;
    if (!tail) tail = head;
}

// Add response to cache
void add_to_cache(const char *url, const char *response) {
    CacheNode *node = create_node(url, response);
    if (cache_count == CACHE_SIZE) {
        CacheNode *to_remove = tail;
        tail = tail->prev;
        if (tail) tail->next = NULL;
        free(to_remove->url);
        free(to_remove->response);
        free(to_remove);
        cache_count--;
    }
    node->next = head;
    if (head) head->prev = node;
    head = node;
    if (!tail) tail = head;
    cache_count++;
}

// Find response in cache
char* find_in_cache(const char *url) {
    CacheNode *node = head;
    while (node) {
        if (strcmp(node->url, url) == 0) {
            move_to_head(node);
            return node->response;
        }
        node = node->next;
    }
    return NULL;
}

// GUI components
GtkWidget *window, *view;
GtkTextBuffer *buffer;

// Append log to GUI
void log_message(const char *message) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
}

// GUI setup
void setup_gui() {
    gtk_init(NULL, NULL);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Proxy Server Logs");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_container_add(GTK_CONTAINER(window), scrolled);
    gtk_widget_show_all(window);
}

// Log cache hit/miss
void log_cache_event(const char *url, int hit) {
    char message[1024];
    snprintf(message, sizeof(message), "%s: Cache %s", url, hit ? "Hit" : "Miss");
    log_message(message);
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

// Tunnel HTTP/HTTPS data
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

// Handle HTTP/HTTPS requests
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

    // Handle HTTPS requests (CONNECT method)
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
            strncpy(path, path_start, sizeof(path) - 1);
        } else {
            strncpy(host, host_start, sizeof(host) - 1);
            strcpy(path, "/");
        }
    }

    if (is_blocked(host)) {
        const char *forbidden = "HTTP/1.1 403 Forbidden\r\n\r\n";
        send(client_socket, forbidden, strlen(forbidden), 0);
        close(client_socket);
        return NULL;
    }

    char *cached_response = find_in_cache(url);
    if (cached_response) {
        log_cache_event(url, 1);
        send(client_socket, cached_response, strlen(cached_response), 0);
        close(client_socket);
        return NULL;
    }

    int remote_socket = connect_to_host(host, port);
    if (remote_socket < 0) {
        close(client_socket);
        return NULL;
    }

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s %s %s\r\n", method, path, protocol);
    send(remote_socket, request, strlen(request), 0);

    bytes_received = recv(remote_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        add_to_cache(url, buffer);
        send(client_socket, buffer, bytes_received, 0);
    }

    close(client_socket);
    close(remote_socket);
    return NULL;
}

int main() {
    setup_gui();
    log_message("Proxy server initialized with LRU caching.");
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[-] Unable to create socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[-] Bind failed");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("[-] Listen failed");
        return 1;
    }

    printf("[+] Proxy server running on port %d\n", PORT);

    while (1) {
        int *client_fd = (int *)malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            perror("[-] Accept failed");
            continue;
        }

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}
