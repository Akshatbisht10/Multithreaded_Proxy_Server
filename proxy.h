#ifndef PROXY_H
#define PROXY_H
#include <stddef.h>

void* server_thread_func(void* arg);
void* handle_client(void* arg);
void build_cache_key(const char *method, const char *url, const char *body, char *key, size_t keysize);

#endif
