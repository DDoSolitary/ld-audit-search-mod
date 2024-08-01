cmake_minimum_required(VERSION 3.29)

project(fuck CXX)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)

add_library(ld-audit-search-mod SHARED lib.cpp)
target_compile_definitions(ld-audit-search-mod PRIVATE
    _GNU_SOURCE
    NIX_STORE_DIR="${NIX_STORE_DIR}"
    NIX_RTLD_NAME="${NIX_RTLD_NAME}")
target_link_libraries(ld-audit-search-mod PRIVATE yaml-cpp::yaml-cpp spdlog::spdlog)
target_link_options(ld-audit-search-mod PRIVATE -static-libgcc -static-libstdc++)

install(TARGETS ld-audit-search-mod)
