cmake_minimum_required(VERSION 3.29)

# zig doesn't support --dependency-file currently
# https://github.com/ziglang/zig-bootstrap/issues/206
set(CMAKE_C_LINKER_DEPFILE_SUPPORTED FALSE)
set(CMAKE_CXX_LINKER_DEPFILE_SUPPORTED FALSE)

project(fuck CXX)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)

add_library(ld-audit-search-mod SHARED
    lib.cpp
    aligned-alloc.cpp
    fix-dtv-realloc.cpp)
target_compile_options(ld-audit-search-mod PRIVATE -fno-builtin-malloc)
target_compile_definitions(ld-audit-search-mod PRIVATE
    _GNU_SOURCE
    NIX_STORE_DIR="${NIX_STORE_DIR}"
    NIX_RTLD_NAME="${NIX_RTLD_NAME}")
target_link_libraries(ld-audit-search-mod PRIVATE yaml-cpp::yaml-cpp spdlog::spdlog)
target_link_options(ld-audit-search-mod PRIVATE
    -static-libgcc -static-libstdc++
    LINKER:-Bsymbolic
    LINKER:--version-script,${CMAKE_CURRENT_SOURCE_DIR}/lib.map)

install(TARGETS ld-audit-search-mod)
