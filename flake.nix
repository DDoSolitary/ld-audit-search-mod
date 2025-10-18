{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      name = "ld-audit-search-mod";

      myOverlay = import ./overlay.nix;

      eachSystem =
        f:
        nixpkgs.lib.genAttrs
          [
            "x86_64-linux"
            "i686-linux"
            "aarch64-linux"
          ]
          (
            system:
            f (
              import nixpkgs {
                inherit system;
                overlays = [ myOverlay ];
              }
            )
          );
    in
    {
      packages = eachSystem (pkgs: rec {
        default = pkgs.${name};
        ${name} = default;
      });

      devShells.default = eachSystem (
        pkgs:
        (pkgs.mkShell.override { stdenv = pkgs.${name}.stdenv; }) {
          inputsFrom = [ pkgs.${name} ];
          packages = [ pkgs.clang-tools ];
        }
      );

      overlays.default = myOverlay;
    };
}
