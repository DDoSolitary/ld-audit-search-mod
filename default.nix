{ stdenv, spdlog, yaml-cpp, cmake, mimalloc }: stdenv.mkDerivation {
  name = "ld-audit-search-mod";
  src = ./src;
  buildInputs = [ spdlog yaml-cpp mimalloc ];
  nativeBuildInputs = [ cmake ];
  preConfigure = ''
    nix_rtld_path=$(cat ${stdenv.cc}/nix-support/dynamic-linker)
    nix_rtld_name=$(basename $nix_rtld_path)
    cmakeFlagsArray+=(
      "-DNIX_RTLD_NAME=$nix_rtld_name"
      "-DNIX_STORE_DIR=$NIX_STORE"
    )
  '';
}
