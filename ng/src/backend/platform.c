/*
 * Platform Abstraction Layer - Implementation
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#else
#include <sys/mman.h>
#include <dirent.h>
#endif

#include "backend/platform.h"

/* Global error storage */
static int g_platform_errno = 0;

/* Platform initialization */
void platform_init(void) {
    g_platform_errno = 0;
}

void platform_cleanup(void) {
    g_platform_errno = 0;
}

/* Path operations */
void platform_basename(const char* path, char* result) {
    const char* p;
    
    if (!path || !result) return;
    
    p = strrchr(path, '/');
    if (!p) {
#ifdef _WIN32
        p = strrchr(path, '\\');
        if (!p) p = path;
        else p++;
#else
        p = path;
#endif
    } else {
        p++;
    }
    
    strcpy(result, p);
}

void platform_dirname(const char* path, char* result) {
    const char* p;
    size_t len;
    
    if (!path || !result) return;
    
    p = strrchr(path, '/');
    if (!p) {
#ifdef _WIN32
        p = strrchr(path, '\\');
#endif
    }
    
    if (p) {
        len = p - path;
        strncpy(result, path, len);
        result[len] = '\0';
    } else {
        strcpy(result, ".");
    }
}

void platform_join_path(const char* dir, char* result, const char* file) {
    size_t dlen, flen;
    
    if (!dir || !result || !file) return;
    
    dlen = strlen(dir);
    flen = strlen(file);
    
    strcpy(result, dir);
    
    /* Add separator if needed */
    if (dlen > 0 && dir[dlen-1] != '/' 
#ifdef _WIN32
        && dir[dlen-1] != '\\'
#endif
    ) {
#ifdef _WIN32
        strcat(result, "\\");
#else
        strcat(result, "/");
#endif
    }
    
    strcat(result, file);
}

void platform_resolve_path(const char* path, char* result) {
    if (!path || !result) return;
    
    /* Handle ^ prefix (GrADS-style) */
    if (path[0] == '^') {
        const char* gaddir = getenv("GADDIR");
        if (gaddir) {
            platform_join_path(gaddir, result, path + 1);
            return;
        }
    }
    
    strcpy(result, path);
}

int platform_path_exists(const char* path) {
    if (!path) return 0;
    
#ifdef _WIN32
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

int platform_is_absolute(const char* path) {
    if (!path) return 0;
    
#ifdef _WIN32
    if (path[0] == '\\' || (path[0] && path[1] == ':')) return 1;
#else
    if (path[0] == '/') return 1;
#endif
    return 0;
}

/* File operations */
platform_file_t* platform_open_file(const char* path) {
    platform_file_t* file;
    
    if (!path) {
        g_platform_errno = EINVAL;
        return NULL;
    }
    
    file = calloc(1, sizeof(platform_file_t));
    if (!file) {
        g_platform_errno = ENOMEM;
        return NULL;
    }
    
#ifdef _WIN32
    file->fd = _open(path, _O_RDONLY);
    if (file->fd < 0) {
#else
    file->fd = open(path, O_RDONLY);
    if (file->fd < 0) {
#endif
        g_platform_errno = errno;
        free(file);
        return NULL;
    }
    
    /* Get file size */
    struct stat st;
    if (fstat(file->fd, &st) == 0) {
        file->size = st.st_size;
    }
    
    return file;
}

void platform_close_file(platform_file_t* file) {
    if (!file) return;
    
    if (file->fd >= 0) {
#ifdef _WIN32
        _close(file->fd);
#else
        close(file->fd);
#endif
    }
    
    free(file);
}

size_t platform_read_file(platform_file_t* file, void* buf, size_t size, size_t offset) {
    ssize_t ret;
    
    if (!file || !buf) {
        g_platform_errno = EINVAL;
        return (size_t)-1;
    }
    
#ifdef _WIN32
    lseek(file->fd, offset, SEEK_SET);
    ret = read(file->fd, buf, size);
#else
    pread(file->fd, buf, size, offset);
    if (ret < 0) {
        ret = -1;
    }
#endif
    
    if (ret < 0) {
        g_platform_errno = errno;
        return (size_t)-1;
    }
    
    return (size_t)ret;
}

size_t platform_get_file_size(const char* path) {
    struct stat st;
    
    if (!path) return 0;
    
    if (stat(path, &st) == 0) {
        return st.st_size;
    }
    
    return 0;
}

/* Memory-mapped file support */
void* platform_mmap_file(const char* path, size_t* size) {
    platform_file_t* file;
    void* addr;
    
    file = platform_open_file(path);
    if (!file) return NULL;
    
    if (size) *size = file->size;
    
#ifdef _WIN32
    addr = NULL;  /* Not implemented on Windows */
#else
    addr = mmap(NULL, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
    if (addr == MAP_FAILED) {
        addr = NULL;
    }
#endif
    
    platform_close_file(file);
    return addr;
}

void platform_munmap_file(void* addr, size_t size) {
    if (!addr) return;
    
#ifndef _WIN32
    munmap(addr, size);
#endif
}

/* Time */
unsigned long platform_get_timestamp(void) {
    return (unsigned long)time(NULL);
}

void platform_sleep(int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

/* Environment */
char* platform_getenv(const char* name) {
    return getenv(name);
}

#ifdef _WIN32
int platform_setenv(const char* name, const char* value) {
    return SetEnvironmentVariable(name, value) ? 0 : -1;
}
#else
int platform_setenv(const char* name, const char* value) {
    return setenv(name, value, 1);
}
#endif

/* Error handling */
int platform_get_error(void) {
    return g_platform_errno;
}

const char* platform_strerror(int err) {
    return strerror(err);
}

/* Threading stubs */
typedef struct {} platform_thread_impl_t;
typedef struct {} platform_mutex_impl_t;

platform_thread_t platform_create_thread(void* (*func)(void*), void* arg) {
    (void)func;
    (void)arg;
    return NULL;
}

void platform_join_thread(platform_thread_t thread) {
    (void)thread;
}

platform_mutex_t platform_create_mutex(void) {
    return NULL;
}

void platform_destroy_mutex(platform_mutex_t mutex) {
    (void)mutex;
}

void platform_lock_mutex(platform_mutex_t mutex) {
    (void)mutex;
}

void platform_unlock_mutex(platform_mutex_t mutex) {
    (void)mutex;
}