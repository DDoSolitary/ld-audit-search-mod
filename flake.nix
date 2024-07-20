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
      myPkgs = pkgs: {
        ld-audit-prefer-runpath = pkgs.callPackage ./. {};
        ld-audit-prefer-runpath-dbg = pkgs.callPackage ./. { enableDebugLog = true; };
      };
    in flake-utils.lib.eachDefaultSystem (system: {
      packages =
        let
          pkgs = import nixpkgs { inherit system; };
        in (myPkgs pkgs) // {
          default = (myPkgs pkgs).ld-audit-prefer-runpath;
        };
      }) // {
        overlays.default = final: prev: myPkgs prev;
      };
}
