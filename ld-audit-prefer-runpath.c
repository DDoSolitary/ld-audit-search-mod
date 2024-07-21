#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG_LOG
#include <stdio.h>
#define DPRINTF(fmt, ...) fprintf(stderr, "%s: " fmt, __func__, __VA_ARGS__)
#define DPUTS(s) fprintf(stderr, "%s: %s\n", __func__, s)
#else
#define DPRINTF(fmt, ...)
#define DPUTS(s)
#endif

// ensure support for old versions of glibc
#if defined(__x86_64__)
// x86_64 Linux goes back to 2.2.5.
#define FORCE_SYMVER(sym) __asm__(".symver " #sym "," #sym "@GLIBC_2.2.5")
#elif defined(__aarch64__)
// aarch64 Linux only goes back to 2.17.
#define FORCE_SYMVER(sym) __asm__(".symver " #sym "," #sym "@GLIBC_2.17")
#else
#error "unsupported architecture"
#endif
FORCE_SYMVER(__errno_location);
FORCE_SYMVER(close);
FORCE_SYMVER(dl_iterate_phdr);
FORCE_SYMVER(fprintf);
FORCE_SYMVER(getenv);
FORCE_SYMVER(open);
FORCE_SYMVER(read);
FORCE_SYMVER(stderr);
FORCE_SYMVER(strlen);
FORCE_SYMVER(strncmp);

#define ELFW(x) ELFW_1(x, __ELF_NATIVE_CLASS)
#define ELFW_1(x, y) ELFW_2(x, y)
#define ELFW_2(x, y) ELF##x##y

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

// mimick the behavior of open_path and open_verify
// https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-load.c#L1918
// in case of fatal errors, we should return the path back to ld.so so that it
// can detect the error and abort the search process
static int is_fatal_err() {
  int err = errno;
  return err != ENOENT && err != ENOTDIR && err != EACCES;
}

static int try_path(const char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return is_fatal_err();
  }
  ElfW(Ehdr) ehdr;
  size_t off = 0;
  while (off < sizeof(ehdr)) {
    ssize_t ret = read(fd, &ehdr, sizeof(ehdr) - off);
    if (ret == 0) {
      DPUTS("file too short");
      close(fd);
      return 1;
    }
    if (ret < 0) {
      DPUTS("read error");
      close(fd);
      return is_fatal_err();
    }
    off += ret;
  }
  close(fd);

  if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
      ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
    DPUTS("invalid ELF magic number");
    return 1;
  }

  if (ehdr.e_ident[EI_CLASS] != ELFW(CLASS)) {
    DPUTS("word size mismatch");
    // multilib support
    return 0;
  }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  if (ehdr.e_ident[EI_DATA] != ELFDATA2MSB) {
#else
#error "unsupported byte order"
#endif
    DPUTS("byte order mismatch");
    return 1;
  }

  if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
    DPUTS("ELF version mismatch");
    return 1;
  }

  // https://github.com/bminor/glibc/blob/glibc-2.39/sysdeps/gnu/ldsodefs.h
  if (ehdr.e_ident[EI_OSABI] != ELFOSABI_SYSV &&
      ehdr.e_ident[EI_OSABI] != ELFOSABI_GNU) {
    DPUTS("OS ABI mismatch");
    return 1;
  }
  if (!(ehdr.e_ident[EI_ABIVERSION] == 0 ||
        (ehdr.e_ident[EI_OSABI] == ELFOSABI_GNU &&
         // no way to retrieve LIBC_ABI_MAX at runtime
         ehdr.e_ident[EI_ABIVERSION] < 4))) {
    DPUTS("ABI version mismatch");
    return 1;
  }

  for (int i = EI_PAD; i < EI_NIDENT; i++) {
    if (ehdr.e_ident[i]) {
      DPUTS("non-zero padding in e_ident");
      return 1;
    }
  }

#ifdef __x86_64__
  if (ehdr.e_machine != EM_X86_64) {
#elif __aarch64__
  if (ehdr.e_machine != EM_AARCH64) {
#endif
    DPUTS("arch mismatch");
    return 0;
  }

  // we don't care about other fatal errors
  // the next and last non-fatal error check is elf_machine_reject_phdr_p, which
  // is only defined for MIPS

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

// something like strtok, but does not change the input string
static int iterate_delim_str(const char *s, char c,
                             int (*callback)(const char *, size_t, void *),
                             void *data) {
  if (!s) {
    return 0;
  }
  while (1) {
    const char *next = s;
    while (*next != c && *next) {
      next++;
    }
    if (next != s) {
      int ret = callback(s, next - s, data);
      if (ret) {
        return ret;
      }
    }
    if (!*next) {
      break;
    }
    s = next + 1;
  }
  return 0;
}

static int check_lib_name(const char *lib, size_t len, void *data) {
  DPRINTF("%.*s\n", (int)len, lib);
  const char *cur_lib = (const char *)data;
  size_t cur_lib_len = strlen(cur_lib);
  return len == cur_lib_len && strncmp(lib, cur_lib, len) == 0;
}

static int enabled;

unsigned int la_version(unsigned int version) {
  if (getenv("LAPR_ENABLE_FOR_ALL")) {
    enabled = 1;
  } else {
    // check if the executable is built for Nix
    int nix_rtld_found = dl_iterate_phdr(&check_nix_rtld, NULL);

    // we don't want to change behavior of binaries not built for Nix according
    // to manual returning 0 here disables this audit module, but it causes
    // crashes on some old versions of glibc, so we maintain the enable flag
    // ourselves
    enabled = nix_rtld_found;
  }

  DPRINTF("version=%u LAV_CURRENT=%u enabled=%d\n", version, LAV_CURRENT,
          enabled);

  // current version is 2, version 1 only differs in la_symbind
  // https://github.com/bminor/glibc/commit/32612615c58b394c3eb09f020f31310797ad3854
  if (version <= 2) {
    return version;
  }
  return LAV_CURRENT;
}

char *la_objsearch(const char *name, uintptr_t *cookie, unsigned int flag) {
  if (!enabled) {
    DPUTS("disabled");
    return (char *)name;
  }

  DPRINTF("cookie=%p flag=%u name=%s\n", cookie, flag, name);

  static char name_buf[PATH_MAX];
  static int libpath_found;
  static int ignore;
  static int disable_libpath;

  // initialization
  if (flag == LA_SER_ORIG) {
    libpath_found = 0;
    char *ignore_libs = getenv("LAPR_IGNORE_LIBS");
    ignore = iterate_delim_str(ignore_libs, ':', &check_lib_name, name);
    char *disable_libpath_libs = getenv("LAPR_DISABLE_LIBPATH_LIBS");
    disable_libpath =
        iterate_delim_str(disable_libpath_libs, ':', &check_lib_name, name);
  }

  if (ignore) {
    DPUTS("ignored");
    return (char *)name;
  }

  if (flag == LA_SER_LIBPATH && disable_libpath) {
    DPUTS("libpath disabled");
    return NULL;
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
