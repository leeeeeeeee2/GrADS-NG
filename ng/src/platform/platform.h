#ifndef NG_PLATFORM_H
#define NG_PLATFORM_H

/*
 * ng/src/platform/platform.h
 * Cross-platform portability layer for GrADS-NG.
 *
 * Rules:
 *   - No GrADS-specific types here; only OS-level abstractions.
 *   - All platform ifdefs live in this header and platform.c, nowhere else.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <direct.h>
#  define NG_PATH_SEP '\\'
#  define NG_PATH_SEP_STR "\\"
#  define NG_INLINE __forceinline
   typedef __int64 ng_off_t;
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  include <dlfcn.h>
#  define NG_PATH_SEP '/'
#  define NG_PATH_SEP_STR "/"
#  define NG_INLINE static inline
   typedef off_t ng_off_t;
#  ifndef strcasecmp
#    include <strings.h>
#  endif
#endif

/* Path utilities */
char *ng_path_normalize(const char *path);   /* caller frees */
int   ng_path_is_absolute(const char *path);
int   ng_mkdir_p(const char *path);          /* recursive mkdir */

/* Dynamic loading */
void *ng_dlopen(const char *path);
void *ng_dlsym(void *handle, const char *sym);
void  ng_dlclose(void *handle);
const char *ng_dlerror(void);

/* Memory-mapped file */
typedef struct ng_mmap ng_mmap_t;
ng_mmap_t  *ng_mmap_open(const char *path, size_t *size_out);
const void *ng_mmap_data(ng_mmap_t *m);
void        ng_mmap_close(ng_mmap_t *m);

#endif /* NG_PLATFORM_H */
