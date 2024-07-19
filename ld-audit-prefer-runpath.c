// use of libc functions is specifically avoided so that this library can be
// loaded in environments with old glibc

#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <stddef.h>
#include <sys/syscall.h>

#ifdef DEBUG_LOG
#include <stdio.h>
#define DPRINTF(fmt, ...) printf("%s: " fmt, __func__, __VA_ARGS__)
#define DPUTS(s) printf("%s: %s\n", __func__, s)
#else
#define DPRINTF(fmt, ...)
#define DPUTS(s)
#endif

// _rtld_global._dl_ns[0]._ns_loaded
// https://github.com/bminor/glibc/blob/910aae6e5a2196938fc30fa54dd1e96f16774ce7/sysdeps/generic/ldsodefs.h#L318
extern struct link_map *_rtld_global;

static int startswith(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    if (*a != *b) {
      return 0;
    }
  }
  return !*b;
}

static int endswith(const char *a, const char *b) {
  int i = 0;
  while (a[i]) {
    i++;
  }
  int j = 0;
  while (b[j]) {
    j++;
  }
  for (; i >= 0 && j >= 0; i--, j--) {
    if (a[i] != b[j]) {
      return 0;
    }
  }
  return j < 0;
}

static int try_path(const char *path) {
  // TODO: mimick behavior of open_verify in elf/dl-load.c and check validity of
  // the ELF file, so that invalid or incompatible libraries can be skipped
  int fd;
  asm volatile("syscall"
               : "=a"(fd)
               : "a"(__NR_open), "D"(path), "S"(O_RDONLY)
               : "rcx", "r11", "memory");
  if (fd < 0) {
    return 0;
  }
  // we don't care about return value of close actually
  int ret;
  asm volatile("syscall"
               : "=a"(ret)
               : "a"(__NR_close), "D"(fd)
               : "rcx", "r11", "memory");
  return 1;
}

static int enabled;

unsigned int la_version(unsigned int version) {
  // find path of ld.so
  int nix_rtld_found = 0;
  for (struct link_map *m = _rtld_global; m; m = m->l_next) {
    char *name = m->l_name;
    if (name && startswith(name, NIX_STORE_DIR "/") &&
        endswith(name, "/" NIX_RTLD_NAME)) {
      DPRINTF("nix ld.so found at %s\n", name);
      nix_rtld_found = 1;
      break;
    }
  }

  // we don't want to change behavior of binaries not built for Nix
  // according to manual returning 0 here disables this audit module, but it
  // causes crashes on some old versions of glibc, so we maintain the enable
  // flag ourselves
  enabled = nix_rtld_found;

  DPRINTF("version=%u LAV_CURRENT=%u enabled=%d\n", version, LAV_CURRENT,
          enabled);

  // current version is 2, version 1 only differs in la_symbind
  // https://github.com/bminor/glibc/commit/32612615c58b394c3eb09f020f31310797ad3854
  if (version <= 2) {
    return version;
  }
  return LAV_CURRENT;
}

static char name_buf[PATH_MAX];
static int libpath_found;

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
  if (!enabled) {
    DPUTS("disabled");
    return (char *)name;
  }

  DPRINTF("cookie=%p flag=%u name=%s\n", cookie, flag, name);

  // initialization
  if (flag == LA_SER_ORIG) {
    libpath_found = 0;
  }

  // save the matching entry from LD_LIBRARY_PATH and return it after searching
  // DT_RUNPATH
  if (flag == LA_SER_LIBPATH && try_path(name)) {
    DPRINTF("libpath match %s\n", name);
    // only save the first match
    if (!libpath_found) {
      int i = 0;
      for (; i < PATH_MAX - 1 && name[i]; i++) {
        name_buf[i] = name[i];
      }
      name_buf[i] = 0;
      libpath_found = 1;
    }
    return NULL;
  }

  // LA_SER_CONFIG follows LA_SER_RUNPATH, but ldconfig cache may not exist, so
  // we check for the next type LA_SER_DEFAULT as well
  if ((flag == LA_SER_CONFIG || flag == LA_SER_DEFAULT) && libpath_found) {
    DPRINTF("replace %s => %s\n", name, name_buf);
    libpath_found = 0;
    return name_buf;
  }
  return (char *)name;
}