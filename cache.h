#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

typedef struct CacheNode {
    char *key;
    char *response;
    struct CacheNode *prev, *next;
} CacheNode;

extern CacheNode *head;
extern CacheNode *tail;
extern int cache_count;

void log_cache_event(const char *key, int hit);
void cache_init();
void cache_cleanup();
CacheNode* create_node(const char *key, const char *response);
void move_to_head(CacheNode *node);
void add_to_cache(const char *key, const char *response);
char* find_in_cache(const char *key);

#endif
