{ stdenv, lib, enableDebugLog ? false }: stdenv.mkDerivation rec {
  name = "ld-audit-prefer-runpath";
  src = ./.;
  buildPhase = ''
    nix_rtld_path=$(cat ${stdenv.cc}/nix-support/dynamic-linker)
    nix_rtld_name=$(basename $nix_rtld_path)
    nix_store_dir=$(dirname $out)
    $CC -shared -fPIC -D_GNU_SOURCE \
      -DNIX_RTLD_NAME=\"$nix_rtld_name\" \
      -DNIX_STORE_DIR=\"$nix_store_dir\" \
      ${lib.optionalString enableDebugLog "-DDEBUG_LOG"} \
      -nostartfiles \
      -o ${name}.so ${name}.c
    # we only want the _rtld_global symbol, which requires libc during linking due to symbol versioning, but is actually provided by ld.so
    patchelf --remove-rpath ${lib.optionalString (!enableDebugLog) "--remove-needed libc.so.6"} ${name}.so
  '';
  installPhase = ''
    install -Dm644 ${name}.so -t $out/lib
  '';
}
