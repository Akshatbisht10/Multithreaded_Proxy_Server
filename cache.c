#include "cache.h"
#include "gui.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define CACHE_SIZE 10

CacheNode *head = NULL, *tail = NULL;
int cache_count = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

CacheNode* create_node(const char *key, const char *response) {
    CacheNode *node = (CacheNode *)malloc(sizeof(CacheNode));
    node->key = strdup(key);
    node->response = strdup(response);
    node->prev = node->next = NULL;
    return node;
}

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

void add_to_cache(const char *key, const char *response) {
    pthread_mutex_lock(&cache_mutex);
    CacheNode *node = create_node(key, response);
    if (cache_count == CACHE_SIZE) {
        CacheNode *to_remove = tail;
        tail = tail->prev;
        if (tail) tail->next = NULL;
        free(to_remove->key);
        free(to_remove->response);
        free(to_remove);
        cache_count--;
    }
    node->next = head;
    if (head) head->prev = node;
    head = node;
    if (!tail) tail = head;
    cache_count++;
    pthread_mutex_unlock(&cache_mutex);
}

char* find_in_cache(const char *key) {
    pthread_mutex_lock(&cache_mutex);
    CacheNode *node = head;
    while (node) {
        if (strcmp(node->key, key) == 0) {
            move_to_head(node);
            char *resp = node->response;
            pthread_mutex_unlock(&cache_mutex);
            return resp;
        }
        node = node->next;
    }
    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

void cache_init() {
    head = tail = NULL;
    cache_count = 0;
}

void cache_cleanup() {
    pthread_mutex_lock(&cache_mutex);
    CacheNode *node = head;
    while (node) {
        CacheNode *next = node->next;
        free(node->key);
        free(node->response);
        free(node);
        node = next;
    }
    head = tail = NULL;
    cache_count = 0;
    pthread_mutex_unlock(&cache_mutex);
}

void log_cache_event(const char *key, int hit) {
    char message[1024];
    snprintf(message, sizeof(message), "%s: Cache %s", key, hit ? "Hit" : "Miss");
    char *msg = strdup(message);
    g_idle_add(log_message_idle, msg);
}