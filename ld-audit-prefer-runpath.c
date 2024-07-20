#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG_LOG
#include <stdio.h>
#define DPRINTF(fmt, ...) printf("%s: " fmt, __func__, __VA_ARGS__)
#define DPUTS(s) printf("%s: %s\n", __func__, s)
#else
#define DPRINTF(fmt, ...)
#define DPUTS(s)
#endif

// ensure support for old versions of glibc
#if defined(__aarch64__)
// aarch64 Linux only goes back to 2.17.
__asm__(".symver close,close@GLIBC_2.17");
__asm__(".symver dl_iterate_phdr,dl_iterate_phdr@GLIBC_2.17");
__asm__(".symver open,open@GLIBC_2.17");
__asm__(".symver printf,printf@GLIBC_2.17");
__asm__(".symver strncmp,strncmp@GLIBC_2.17");
__asm__(".symver strlen,strlen@GLIBC_2.17");
#elif defined(__x86_64__)
// x86_64 Linux goes back to 2.2.5.
__asm__(".symver close,close@GLIBC_2.2.5");
__asm__(".symver dl_iterate_phdr,dl_iterate_phdr@GLIBC_2.2.5");
__asm__(".symver open,open@GLIBC_2.2.5");
__asm__(".symver printf,printf@GLIBC_2.2.5");
__asm__(".symver strncmp,strncmp@GLIBC_2.2.5");
__asm__(".symver strlen,strlen@GLIBC_2.2.5");
#endif

static int startswith(const char *a, const char *b) {
  return strncmp(a, b, strlen(b)) == 0;
}

static int endswith(const char *a, const char *b) {
  size_t a_len = strlen(a), b_len = strlen(b);
  if (a_len < b_len) {
    return 0;
  }
  return strncmp(a + a_len - b_len, b, b_len) == 0;
}

static int try_path(const char *path) {
  // TODO: mimick behavior of open_verify in elf/dl-load.c and check validity of
  // the ELF file, so that invalid or incompatible libraries can be skipped
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return 0;
  }
  close(fd);
  return 1;
}

static int check_nix_rtld(struct dl_phdr_info *info, size_t size, void *data) {
  const char *name = info->dlpi_name;
  DPRINTF("checking %s\n", name);
  if (name && startswith(name, NIX_STORE_DIR "/") &&
      endswith(name, "/" NIX_RTLD_NAME)) {
    DPUTS("nix ld.so found");
    // stop iteration
    return 1;
  }
  return 0;
}

static int enabled;

unsigned int la_version(unsigned int version) {
  // check if the executable is built for Nix
  int nix_rtld_found = dl_iterate_phdr(&check_nix_rtld, NULL);

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
