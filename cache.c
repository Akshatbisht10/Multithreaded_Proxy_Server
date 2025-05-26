#include "cache.h"
#include "gui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#define CACHE_SIZE 10

CacheNode *head = NULL, *tail = NULL;
int cache_count = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

CacheNode* create_node(const char *key, const char *response) {
    CacheNode *node = (CacheNode *)malloc(sizeof(CacheNode));
    if (!node) return NULL;
    
    node->key = strdup(key);
    if (!node->key) {
        free(node);
        return NULL;
    }
    
    node->response = strdup(response);
    if (!node->response) {
        free(node->key);
        free(node);
        return NULL;
    }
    
    node->prev = node->next = NULL;
    return node;
}

void move_to_head(CacheNode *node) {
    if (!node || node == head) return;
    
    if (node == tail) {
        tail = tail->prev;
        if (tail) tail->next = NULL;
    } else {
        if (node->prev) node->prev->next = node->next;
        if (node->next) node->next->prev = node->prev;
    }
    
    node->next = head;
    node->prev = NULL;
    if (head) head->prev = node;
    head = node;
    if (!tail) tail = head;
}

void add_to_cache(const char *key, const char *response) {
    if (!key || !response) return;
    
    pthread_mutex_lock(&cache_mutex);
    
    printf("[CACHE DEBUG] Adding to cache - Key: %s\n", key);
    
    // Check if key already exists
    CacheNode *existing = NULL;
    CacheNode *current = head;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            existing = current;
            printf("[CACHE DEBUG] Found existing key in cache\n");
            break;
        }
        current = current->next;
    }
    
    if (existing) {
        // Update existing node
        char *new_response = strdup(response);
        if (new_response) {
            free(existing->response);
            existing->response = new_response;
            move_to_head(existing);
            printf("[CACHE DEBUG] Updated existing cache entry\n");
        }
        pthread_mutex_unlock(&cache_mutex);
        return;
    }
    
    // Create new node
    CacheNode *node = create_node(key, response);
    if (!node) {
        printf("[CACHE DEBUG] Failed to create cache node\n");
        pthread_mutex_unlock(&cache_mutex);
        return;
    }
    
    // Remove oldest entry if cache is full
    if (cache_count == CACHE_SIZE && tail) {
        CacheNode *to_remove = tail;
        tail = tail->prev;
        if (tail) tail->next = NULL;
        else head = NULL;
        
        printf("[CACHE DEBUG] Removing oldest entry: %s\n", to_remove->key);
        free(to_remove->key);
        free(to_remove->response);
        free(to_remove);
        cache_count--;
    }
    
    // Add new node to head
    node->next = head;
    if (head) head->prev = node;
    head = node;
    if (!tail) tail = head;
    cache_count++;
    printf("[CACHE DEBUG] Added new entry to cache. Total entries: %d\n", cache_count);
    
    pthread_mutex_unlock(&cache_mutex);
}

char* find_in_cache(const char *key) {
    if (!key) return NULL;
    
    pthread_mutex_lock(&cache_mutex);
    printf("[CACHE DEBUG] Searching for key: %s\n", key);
    
    CacheNode *node = head;
    while (node) {
        printf("[CACHE DEBUG] Comparing with cached key: %s\n", node->key);
        if (strcmp(node->key, key) == 0) {
            move_to_head(node);
            char *resp = strdup(node->response);
            printf("[CACHE DEBUG] Cache HIT!\n");
            pthread_mutex_unlock(&cache_mutex);
            return resp;
        }
        node = node->next;
    }
    printf("[CACHE DEBUG] Cache MISS!\n");
    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

void cache_init() {
    pthread_mutex_lock(&cache_mutex);
    head = tail = NULL;
    cache_count = 0;
    pthread_mutex_unlock(&cache_mutex);
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
    if (!key) return;
    char message[1024];
    snprintf(message, sizeof(message), "%s: Cache %s", key, hit ? "Hit" : "Miss");
    char *msg = strdup(message);
    if (msg) g_idle_add(log_message_idle, msg);
}