final: prev: let
  name = "ld-audit-search-mod";
  glibcTargetVersion = "2.17";
  stdenvZig = final.overrideCC
    final.pkgsZig.stdenv
    (final.pkgsZig.stdenv.cc.override (old : {
      wrapCCWith = args: old.wrapCCWith (final.lib.recursiveUpdate args {
        nixSupport.cc-cflags = args.nixSupport.cc-cflags ++ [
          "-target" "${final.hostPlatform.system}-gnu.${glibcTargetVersion}"
        ];
      });
    }));
    stdenvZigStatic = (final.makeStatic stdenvZig).override (old: {
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
    mimalloc = (final.mimalloc.override { inherit (self) stdenv; }).overrideAttrs (old: {
      postPatch = (old.postPatch or "") + ''
        find include/mimalloc -type f -name '*.h' -exec sed -i '/^#define  MI_USE_PTHREADS$/d' {} \;
      '';
    });
    ${name} = self.callPackage ./. { stdenv = stdenvZig; };
  });
in {
  ${name} = scope.${name};
}
