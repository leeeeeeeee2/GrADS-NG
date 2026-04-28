/*
 * Platform Abstraction Layer for GrADS-NG
 * 
 * Provides cross-platform compatibility for Windows, macOS, and Linux.
 */

#ifndef GRADS_NG_PLATFORM_H
#define GRADS_NG_PLATFORM_H

#include <stddef.h>

/* RGBA color structure */
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} rgba_t;

/* File info */
typedef struct {
    int fd;
    size_t size;
    void* mmap_addr;
} platform_file_t;

/* Platform initialization */
void platform_init(void);
void platform_cleanup(void);

/* Path operations */
void platform_basename(const char* path, char* result);
void platform_dirname(const char* path, char* result);
void platform_join_path(const char* dir, const char* file, char* result);
void platform_resolve_path(const char* path, char* result);
int platform_path_exists(const char* path);
int platform_is_absolute(const char* path);

/* File operations */
platform_file_t* platform_open_file(const char* path);
void platform_close_file(platform_file_t* file);
size_t platform_read_file(platform_file_t* file, void* buf, size_t size, size_t offset);
size_t platform_get_file_size(const char* path);

/* Memory-mapped file support */
void* platform_mmap_file(const char* path, size_t* size);
void platform_munmap_file(void* addr, size_t size);

/* Time */
unsigned long platform_get_timestamp(void);
void platform_sleep(int milliseconds);

/* Threading (stub for future) */
typedef void* platform_thread_t;
typedef void* platform_mutex_t;

platform_thread_t platform_create_thread(void* (*func)(void*), void* arg);
void platform_join_thread(platform_thread_t thread);
platform_mutex_t platform_create_mutex(void);
void platform_destroy_mutex(platform_mutex_t mutex);
void platform_lock_mutex(platform_mutex_t mutex);
void platform_unlock_mutex(platform_mutex_t mutex);

/* Environment */
char* platform_getenv(const char* name);
int platform_setenv(const char* name, const char* value);

/* Error handling */
int platform_get_error(void);
const char* platform_strerror(int err);

#endif /* GRADS_NG_PLATFORM_H */