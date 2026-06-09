final: prev: let
  name = "ld-audit-search-mod";
  glibcTargetVersion = "2.17";
  stdenvZig = final.zig_0_15.stdenv.override {
    cc = final.zig_0_15.cc.override (old: {
      # https://github.com/NixOS/nixpkgs/pull/463199
      cc = old.cc.overrideAttrs (old: {
        passthru = old.passthru // {
          hardeningUnsupportedFlags = old.passthru.hardeningUnsupportedFlags or [] ++ [
            "libcxxhardeningfast"
            "libcxxhardeningextensive"
          ];
        };
      });
      # Target an old glibc version to make it compatible with old host systems.
      nixSupport = final.lib.recursiveUpdate old.nixSupport {
        cc-cflags = old.nixSupport.cc-cflags ++ [
          "-target"
          "${final.stdenv.targetPlatform.system}-gnu.${glibcTargetVersion}"
        ];
      };
    });
  };
  stdenvZigStatic = stdenvZig.override (old: {
    hostPlatform = old.hostPlatform // { isStatic = true; };
  });
  scope = final.lib.makeScope final.newScope (self: {
    stdenv = stdenvZigStatic;
    fmt = (final.fmt.override { inherit (self) stdenv; }).overrideAttrs (old: {
      cmakeFlags = (old.cmakeFlags or []) ++ [
        "-DFMT_TEST=OFF"
      ];
      doCheck = false;
    });
    spdlog = (final.spdlog.override { inherit (self) stdenv fmt; }).overrideAttrs (old: {
      cmakeFlags = (old.cmakeFlags or []) ++ [
        "-DSPDLOG_BUILD_TESTS=OFF"
      ];
      doCheck = false;
    });
    yaml-cpp = final.yaml-cpp.override { inherit (self) stdenv; };
    ${name} = self.callPackage ./. { stdenv = stdenvZig; };
  });
in {
  ${name} = scope.${name};
}
