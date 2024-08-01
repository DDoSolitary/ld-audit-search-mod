{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs?ref=nixpkgs-unstable";
    systems.url = "github:nix-systems/default-linux";
    flake-utils = {
      url = "github:numtide/flake-utils";
      inputs.systems.follows = "systems";
    };
  };
  outputs = { self, flake-utils, nixpkgs, ... }:
    let
      zigTarget = pkgs: "${pkgs.system}-gnu.2.17";
      overrideCMakeZigCC = pkgs: drv: drv.overrideAttrs (old: {
        nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.zig ];
        cmakeFlags = (old.cmakeFlags or []) ++ [
          "-DCMAKE_C_COMPILER=zig;cc;-target;${zigTarget pkgs}"
          "-DCMAKE_CXX_COMPILER=zig;c++;-target;${zigTarget pkgs}"
        ];
        preConfigure = (old.preConfigure or "") + ''
          export ZIG_GLOBAL_CACHE_DIR=$(mktemp -d)
        '';
      });
      pkgsZig = pkgs: pkgs.extend (final: prev: {
        fmt = ((overrideCMakeZigCC prev prev.fmt).override {
          enableShared = false;
        }).overrideAttrs (old: {
          cmakeFlags = (old.cmakeFlags or []) ++ [
            "-DFMT_TEST=OFF"
          ];
          doCheck = false;
        });
        spdlog = ((overrideCMakeZigCC prev prev.spdlog).override {
          staticBuild = true;
        }).overrideAttrs (old: {
          cmakeFlags = (old.cmakeFlags or []) ++ [
            "-DSPDLOG_BUILD_TESTS=OFF"
          ];
          doCheck = false;
        });
        yaml-cpp = (overrideCMakeZigCC prev prev.yaml-cpp).overrideAttrs (old: {
          cmakeFlags = (old.cmakeFlags or []) ++ [
            "-DYAML_BUILD_SHARED_LIBS=OFF"
          ];
        });
      });
      myOverlay = final: prev: rec {
        ld-audit-search-mod = overrideCMakeZigCC prev ((pkgsZig prev).callPackage ./. {});
        defaultPackage = ld-audit-search-mod;
      };
    in flake-utils.lib.simpleFlake {
      inherit self nixpkgs;
      name = "ld-audit-search-mod";
      overlay = final: prev: { ld-audit-search-mod = myOverlay final prev; };
      shell = { pkgs }: pkgs.mkShell {
        inputsFrom = [ pkgs.ld-audit-search-mod.defaultPackage ];
        packages = [ pkgs.clang-tools ];
      };
    } // {
      overlays.default = myOverlay;
    };
}
