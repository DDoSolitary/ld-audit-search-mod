{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs?ref=nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = { self, flake-utils, nixpkgs, ... }:
    let
      myOverlay = import ./overlay.nix;
    in flake-utils.lib.eachSystem [
      "x86_64-linux"
      "i686-linux"
      "aarch64-linux"
    ] (system: let
      pkgs = import nixpkgs { inherit system; overlays = [ myOverlay ]; };
      name = "ld-audit-search-mod";
    in {
      packages = rec {
        default = pkgs.${name};
        ${name} = default;
      };
      devShells.default = (pkgs.mkShell.override { stdenv = pkgs.${name}.stdenv; }) {
        inputsFrom = [ pkgs.${name} ];
        packages = [ pkgs.clang-tools ];
      };
    }) // {
      overlays.default = myOverlay;
    };
}
