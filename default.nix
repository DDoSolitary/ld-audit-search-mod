{ stdenv, lib, enableDebugLog ? false }: stdenv.mkDerivation rec {
  name = "ld-audit-prefer-runpath";
  src = ./.;
  hardeningDisable = [
    "fortify" "stackprotector" # get rid of __*_chk symbols
  ];
  buildPhase = ''
    nix_rtld_path=$(cat ${stdenv.cc}/nix-support/dynamic-linker)
    nix_rtld_name=$(basename $nix_rtld_path)
    nix_store_dir=$(dirname $out)
    $CC -v -shared -fPIC \
      -D_GNU_SOURCE `# needed for auditing interface` \
      -DNIX_RTLD_NAME=\"$nix_rtld_name\" \
      -DNIX_STORE_DIR=\"$nix_store_dir\" \
      ${lib.optionalString enableDebugLog "-DDEBUG_LOG"} \
      -nostartfiles `# get rid of __cxa_finalize` \
      -o ${name}.so ${name}.c
    # remove the rpath for glibc so that system's default glibc will be used
    patchelf --remove-rpath ${name}.so
  '';
  installPhase = ''
    install -Dm644 ${name}.so -t $out/lib
  '';
}
