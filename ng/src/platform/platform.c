#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <fcntl.h>
#endif

/* ---------- path utilities ---------- */

char *ng_path_normalize(const char *path) {
    if (!path) return NULL;
    char *out = strdup(path);
    if (!out) return NULL;
    for (char *p = out; *p; p++)
        if (*p == '/' || *p == '\\') *p = NG_PATH_SEP;
    return out;
}

int ng_path_is_absolute(const char *path) {
    if (!path || !*path) return 0;
#ifdef _WIN32
    return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
           (path[0] == NG_PATH_SEP && path[1] == NG_PATH_SEP);
#else
    return path[0] == NG_PATH_SEP;
#endif
}

int ng_mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len-1] == NG_PATH_SEP) tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == NG_PATH_SEP) {
            *p = '\0';
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = NG_PATH_SEP;
        }
    }
#ifdef _WIN32
    return _mkdir(tmp);
#else
    return mkdir(tmp, 0755);
#endif
}

/* ---------- dynamic loading ---------- */

void *ng_dlopen(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
#endif
}

void *ng_dlsym(void *handle, const char *sym) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)handle, sym);
#else
    return dlsym(handle, sym);
#endif
}

void ng_dlclose(void *handle) {
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

const char *ng_dlerror(void) {
#ifdef _WIN32
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                   0, buf, sizeof(buf), NULL);
    return buf;
#else
    return dlerror();
#endif
}

/* ---------- memory-mapped files ---------- */

struct ng_mmap {
    const void *data;
    size_t      size;
#ifdef _WIN32
    HANDLE fh, mh;
#else
    int fd;
#endif
};

ng_mmap_t *ng_mmap_open(const char *path, size_t *size_out) {
    ng_mmap_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;

#ifdef _WIN32
    m->fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m->fh == INVALID_HANDLE_VALUE) { free(m); return NULL; }
    LARGE_INTEGER sz;
    GetFileSizeEx(m->fh, &sz);
    m->size = (size_t)sz.QuadPart;
    m->mh = CreateFileMappingA(m->fh, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m->mh) { CloseHandle(m->fh); free(m); return NULL; }
    m->data = MapViewOfFile(m->mh, FILE_MAP_READ, 0, 0, 0);
    if (!m->data) { CloseHandle(m->mh); CloseHandle(m->fh); free(m); return NULL; }
#else
    m->fd = open(path, O_RDONLY);
    if (m->fd < 0) { free(m); return NULL; }
    struct stat st;
    if (fstat(m->fd, &st) < 0) { close(m->fd); free(m); return NULL; }
    m->size = (size_t)st.st_size;
    m->data = mmap(NULL, m->size, PROT_READ, MAP_PRIVATE, m->fd, 0);
    if (m->data == MAP_FAILED) { close(m->fd); free(m); return NULL; }
#endif

    if (size_out) *size_out = m->size;
    return m;
}

const void *ng_mmap_data(ng_mmap_t *m) { return m ? m->data : NULL; }

void ng_mmap_close(ng_mmap_t *m) {
    if (!m) return;
#ifdef _WIN32
    UnmapViewOfFile(m->data);
    CloseHandle(m->mh);
    CloseHandle(m->fh);
#else
    munmap((void *)m->data, m->size);
    close(m->fd);
#endif
    free(m);
}
