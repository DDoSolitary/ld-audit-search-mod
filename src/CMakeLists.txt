cmake_minimum_required(VERSION 3.29)

project(fuck CXX)

find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(mimalloc REQUIRED)

get_property(MIMALLOC_STATIC_LIBRARY TARGET mimalloc-static PROPERTY IMPORTED_LOCATION_RELEASE)
cmake_path(REPLACE_FILENAME MIMALLOC_STATIC_LIBRARY mimalloc.o OUTPUT_VARIABLE MIMALLOC_OBJECT_LIBRARY)
add_library(ld-audit-search-mod SHARED "${MIMALLOC_OBJECT_LIBRARY}" lib.cpp)
target_compile_definitions(ld-audit-search-mod PRIVATE
    _GNU_SOURCE
    NIX_STORE_DIR="${NIX_STORE_DIR}"
    NIX_RTLD_NAME="${NIX_RTLD_NAME}")
target_link_libraries(ld-audit-search-mod PRIVATE yaml-cpp::yaml-cpp spdlog::spdlog)
target_link_options(ld-audit-search-mod PRIVATE -static-libgcc -static-libstdc++)

install(TARGETS ld-audit-search-mod)
