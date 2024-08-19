#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <limits.h>
#include <link.h>
#include <optional>
#include <regex>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#define CHECK_EXCEPT_BEGIN try {
#define CHECK_EXCEPT_END                                                       \
  }                                                                            \
  catch (const std::exception &ex) {                                           \
    enabled = false;                                                           \
    fprintf(stderr, "[%s] uncaught exception: %s\n", __func__, ex.what());     \
  }

#define ELFW(x) ELFW_1(x, __ELF_NATIVE_CLASS)
#define ELFW_1(x, y) ELFW_2(x, y)
#define ELFW_2(x, y) ELF##x##y

namespace {
// When we want to ignore a path in la_objsearch, we should return this instead
// of nullptr, so that the search process will not be interrupted.
// See: https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-load.c#L1918
// If nullptr is returned from la_objsearch, but the parent directory of the
// original path exists, open_path has fd = -1, here_any = 1, errno = 0.
// In such cases, open_path would mistakenly think that there's a fatal error
// and return immediately, ignoring subsequent search items.
char non_existent_path[] = "/proc/-1";

std::optional<YAML::Node> cfg;
bool is_nix_rtld;
bool enabled;

struct search_state {
  YAML::Node rule;
  std::string lib_name;
  std::unordered_map<std::string, std::string> block_states;
  bool has_dt_runpath;
};

std::optional<search_state> cur_state;

std::string search_flag_to_str(unsigned flag) {
  switch (flag) {
  case LA_SER_ORIG:
    return "LA_SER_ORIG";
  case LA_SER_RUNPATH:
    return "LA_SER_RUNPATH";
  case LA_SER_LIBPATH:
    return "LA_SER_LIBPATH";
  case LA_SER_CONFIG:
    return "LA_SER_CONFIG";
  case LA_SER_DEFAULT:
    return "LA_SER_DEFAULT";
  case LA_SER_SECURE:
    return "LA_SER_SECURE";
  default:
    return std::to_string(flag);
  }
}

bool is_fatal_err(const char *msg) {
  int err = errno;
  auto err_str = strerror(err);
  bool ret = err != ENOENT && err != ENOTDIR && err != EACCES;
  auto log_level = ret ? spdlog::level::err : spdlog::level::debug;
  SPDLOG_LOGGER_CALL(spdlog::default_logger_raw(), log_level, "{}: {} ({})",
                     msg, err_str, err);
  return ret;
}

// mimick the behavior of open_path and open_verify
// https://github.com/bminor/glibc/blob/glibc-2.39/elf/dl-load.c#L1918
// in case of fatal errors, we should return the path back to ld.so so that it
// can detect the error and abort the search process
bool try_path(const char *path) {
  SPDLOG_DEBUG("path={}", path);
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return is_fatal_err("open failed");
  }
  ElfW(Ehdr) ehdr;
  size_t off = 0;
  while (off < sizeof(ehdr)) {
    ssize_t ret = read(fd, &ehdr, sizeof(ehdr) - off);
    if (ret == 0) {
      SPDLOG_ERROR("file too short");
      close(fd);
      return true;
    }
    if (ret < 0) {
      close(fd);
      return is_fatal_err("read error");
    }
    off += ret;
  }
  close(fd);

  if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
      ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
    SPDLOG_ERROR("invalid ELF magic number");
    return true;
  }

  if (ehdr.e_ident[EI_CLASS] != ELFW(CLASS)) {
    // multilib support
    SPDLOG_DEBUG("word size mismatch");
    return false;
  }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr auto cur_data_enc = ELFDATA2LSB;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  constexpr auto cur_data_enc = ELFDATA2MSB;
#else
#error "unsupported byte order"
#endif
  if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
    SPDLOG_ERROR("byte order mismatch");
    return true;
  }

  if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
    SPDLOG_ERROR("ELF version mismatch");
    return true;
  }

  // https://github.com/bminor/glibc/blob/glibc-2.39/sysdeps/gnu/ldsodefs.h
  if (ehdr.e_ident[EI_OSABI] != ELFOSABI_SYSV &&
      ehdr.e_ident[EI_OSABI] != ELFOSABI_GNU) {
    SPDLOG_ERROR("OS ABI mismatch");
    return true;
  }
  if (!(ehdr.e_ident[EI_ABIVERSION] == 0 ||
        (ehdr.e_ident[EI_OSABI] == ELFOSABI_GNU &&
         // no way to retrieve LIBC_ABI_MAX at runtime
         ehdr.e_ident[EI_ABIVERSION] < 4))) {
    SPDLOG_ERROR("ABI version mismatch");
    return true;
  }

  for (int i = EI_PAD; i < EI_NIDENT; i++) {
    if (ehdr.e_ident[i]) {
      SPDLOG_ERROR("non-zero padding in e_ident");
      return true;
    }
  }

#ifdef __x86_64__
  constexpr auto cur_machine = EM_X86_64;
#elif __aarch64__
  constexpr auto cur_machine = EM_AARCH64;
#else
#error "unsupported architecture"
#endif
  if (ehdr.e_machine != cur_machine) {
    SPDLOG_DEBUG("arch mismatch");
    return false;
  }

  // we don't care about other fatal errors
  // the next and last non-fatal error check is elf_machine_reject_phdr_p,
  // which is only defined for MIPS

  return true;
}
} // namespace

extern "C" {
unsigned la_version(unsigned version) {
  CHECK_EXCEPT_BEGIN

  spdlog::set_default_logger(spdlog::stderr_color_st("ld-audit-search-mod"));
  spdlog::set_pattern("[%!] %v");

  // load config
  auto cfg_path = getenv("LD_AUDIT_SEARCH_MOD_CONFIG");
  if (cfg_path) {
    cfg = YAML::LoadFile(cfg_path);
  }
  if (cfg) {
    spdlog::set_level(spdlog::level::from_str(
        (*cfg)["log_level"].as<std::string>("warning")));
    SPDLOG_DEBUG("version={}, cfg_path={}", version, cfg_path);
    enabled = true;
  }

  // init global state
  is_nix_rtld = dl_iterate_phdr(
      [](dl_phdr_info *info, size_t, void *) -> int {
        CHECK_EXCEPT_BEGIN
        std::string name = info->dlpi_name;
        static const std::string nix_store_dir = NIX_STORE_DIR;
        static const std::string nix_rtld_name = NIX_RTLD_NAME;
        if (name.compare(0, nix_store_dir.size(), nix_store_dir) == 0 &&
            name.size() >= nix_rtld_name.size() &&
            name.compare(name.size() - nix_rtld_name.size(), std::string::npos,
                         nix_rtld_name) == 0) {
          SPDLOG_DEBUG("nix rtld found: {}", name);
          return 1;
        }
        CHECK_EXCEPT_END
        return 0;
      },
      nullptr);

  CHECK_EXCEPT_END

  // current version is 2, version 1 only differs in la_symbind
  // https://github.com/bminor/glibc/commit/32612615c58b394c3eb09f020f31310797ad3854
  if (version <= 2) {
    return version;
  }
  return LAV_CURRENT;
}

char *la_objsearch(const char *name_const, uintptr_t *cookie,
                   unsigned int flag) {
  // it has to be non-const when returned
  auto name = const_cast<char *>(name_const);
  if (!enabled) {
    return name;
  }

  CHECK_EXCEPT_BEGIN

  SPDLOG_DEBUG("cookie={} flag={} name={}", fmt::ptr(cookie),
               search_flag_to_str(flag), name_const);

  // initialization
  if (flag == LA_SER_ORIG) {
    cur_state.emplace();

    // undocumented way to retrieve link_map of the dependent library
    auto lm = *(const link_map *const *)cookie;
    cur_state->has_dt_runpath = false;
    for (auto dyn = lm->l_ld; dyn->d_tag != DT_NULL; dyn++) {
      if (dyn->d_tag == DT_RUNPATH) {
        cur_state->has_dt_runpath = true;
      }
    }
    SPDLOG_DEBUG("l_name={} has_dt_runpath={}", lm->l_name,
                 cur_state->has_dt_runpath);

    cur_state->rule.reset(YAML::Node(YAML::NodeType::Undefined));
    auto rules = (*cfg)["rules"];
    for (size_t i = 0; i < rules.size(); i++) {
      auto rule = rules[i];
      auto rtld_type = rule["cond"]["rtld"].as<std::string>("any");
      if (!(rtld_type == "nix" && is_nix_rtld ||
            rtld_type == "normal" && !is_nix_rtld || rtld_type == "any")) {
        continue;
      }
      if (!std::regex_match(
              name, std::regex(rule["cond"]["lib"].as<std::string>(".*")))) {
        continue;
      }
      if (!std::regex_match(
              lm->l_name,
              std::regex(
                  rule["cond"]["dependent_lib"].as<std::string>(".*")))) {
        continue;
      }
      SPDLOG_DEBUG("rule {} matched", i);
      cur_state->rule.reset(rule);
      break;
    }

    if (cur_state->rule) {
      if (auto rename_node = cur_state->rule["rename"]; rename_node) {
        cur_state->lib_name = rename_node.as<std::string>();
      } else {
        cur_state->lib_name = name;
      }
      return cur_state->lib_name.data();
    } else {
      SPDLOG_DEBUG("no matching rule, skipping");
      return name;
    }
  }

  if (!cur_state->rule) {
    return name;
  }

  std::string rule_block_name;
  switch (flag) {
  case LA_SER_RUNPATH:
    rule_block_name = cur_state->has_dt_runpath ? "runpath" : "rpath";
    break;
  case LA_SER_LIBPATH:
    rule_block_name = "libpath";
    break;
  case LA_SER_CONFIG:
    rule_block_name = "config";
    break;
  case LA_SER_DEFAULT:
    rule_block_name = "default";
    break;
  }
  YAML::Node cur_rule_block;
  if (rule_block_name.empty() ||
      !((cur_rule_block = cur_state->rule[rule_block_name]))) {
    SPDLOG_DEBUG("rule block {} not found, skipping", rule_block_name);
    return name;
  }

  if (cur_state->block_states.emplace(rule_block_name, "").second) {
    for (auto prepend_node : cur_rule_block["prepend"]) {
      auto saved_node = prepend_node["saved"];
      if (saved_node) {
        auto saved_from = saved_node.as<std::string>();
        SPDLOG_DEBUG("saved_from={}", saved_from);
        auto it = cur_state->block_states.find(saved_from);
        if (saved_from == rule_block_name ||
            it == cur_state->block_states.end()) {
          SPDLOG_DEBUG("block {} not searched yet", saved_from);
          continue;
        }
        if (it->second.empty()) {
          SPDLOG_DEBUG("block {} has no saved path", saved_from);
          continue;
        }
        SPDLOG_DEBUG("returning saved path: {}", it->second);
        return it->second.data();
      }

      std::string file_path;
      if (auto file_node = prepend_node["file"]; file_node) {
        file_path = file_node.as<std::string>();
      } else if (auto dir_node = prepend_node["dir"]; dir_node) {
        std::filesystem::path dir_path = dir_node.as<std::string>();
        file_path = dir_path / cur_state->lib_name;
      } else {
        continue;
      }
      if (try_path(file_path.c_str())) {
        return ((cur_state->block_states[rule_block_name] = file_path)).data();
      }
    }
  }

  auto filters = cur_rule_block["filter"];
  for (size_t i = 0; i < filters.size(); i++) {
    auto filter = filters[i];
    if (auto inc_node = filter["include"];
        inc_node &&
        std::regex_match(name, std::regex(inc_node.as<std::string>()))) {
      SPDLOG_DEBUG("filter {} matched", i);
      break;
    }
    if (auto exc_node = filter["exclude"];
        exc_node &&
        std::regex_match(name, std::regex(exc_node.as<std::string>()))) {
      SPDLOG_DEBUG("filter {} matched", i);
      return non_existent_path;
    }
  }

  if (cur_rule_block["save"].as<bool>(false)) {
    if (try_path(name)) {
      if (auto &saved_path = cur_state->block_states[rule_block_name];
          saved_path.empty()) {
        SPDLOG_DEBUG("saving matched path: {}", name);
        saved_path = name;
      }
    }
    return non_existent_path;
  }

  CHECK_EXCEPT_END
  return name;
}

void la_preinit(uintptr_t *cookie) {
  if (!enabled) {
    return;
  }
  CHECK_EXCEPT_BEGIN
  SPDLOG_DEBUG("cookie={}", fmt::ptr(cookie));
  cur_state.reset();
  CHECK_EXCEPT_END
}
}
